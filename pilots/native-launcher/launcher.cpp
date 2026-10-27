/* Bundled catalog adapter for recomp-ui. It stages data, never loads mod code.
 * The game's Python resolver remains the final package/identity authority.
 * Hex UTF-8 fields keep the handoff bounded and independent of shell quoting. */
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "common/launcher_model.h"
#include "visual_mods.h"
#include "console_setup.h"
#include "xbe_identity.h"
#include "controller_handoff.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#if defined(_WIN32)
#include <windows.h>
#endif
XboxllrConsoleSetup *xboxllr_console_setup=nullptr;
struct Choice {std::string value,label;};
struct Option {std::string id,label,value,initial;std::vector<Choice> choices;};
struct Feature {std::string package,id,name,description,group="Other";int enabled=0;std::vector<Option> options;};
struct Package {std::string id,name,version,description;};
struct SavedImage {XboxllrGraffitiDesign info;std::vector<unsigned char> rgba;};
struct Catalog {
    std::string title,region,error;std::vector<std::string> identities;std::vector<Package> packages;std::vector<Feature> features;
    std::string player_source="auto",player_guid;
    RecompLauncherCSettings settings={};
    bool setup=false,needs_setup=false;
    bool console=false;
    bool has_game_id=false;
    uint32_t game_id=0;
    std::string choice_path,import_path;
    bool boot=false;
    std::string boot_choice_path;
    XboxllrConsoleSetup console_ui;
    std::vector<SavedImage> saved_images;
    std::vector<unsigned char> preview_pixels;
    int preview_width=0,preview_height=0;
};
static std::string unhex(const std::string &s) {
    if(s=="-")return {};
    if(s.size()>8192 || s.size()%2)throw std::runtime_error("Invalid launcher field size");
    std::string out;
    for(size_t i=0;i<s.size();i+=2) {
        auto digit=[](char c)->int {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        int a=digit(s[i]),b=digit(s[i+1]);if(a<0||b<0||!(a||b))throw std::runtime_error("Invalid launcher field");
        out.push_back(char(a*16+b));
    }
    return out;
}
static std::string hex(const std::string &s) {
    if(s.empty())return "-";std::string out;const char *digits="0123456789abcdef";
    for(unsigned char c:s){out+=digits[c>>4];out+=digits[c&15];}return out;
}
template<size_t N> static void copy(char (&out)[N],const std::string &s) {
    std::snprintf(out,N,"%s",s.c_str());
}
static Feature *feature(Catalog &c,const char *package,const char *id) {
    for(auto &f:c.features)if(f.package==package&&f.id==id)return &f;return nullptr;
}
static Option *option(Feature *f,const char *id) {
    if(f)for(auto &o:f->options)if(o.id==id)return &o;return nullptr;
}
static std::vector<std::string> fields(const std::string &line) {
    std::istringstream in(line);std::string s;std::vector<std::string> out;while(in>>s)out.push_back(s);return out;
}
static Catalog read_catalog(const char *path) {
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Missing launcher catalog");
    in.seekg(0,std::ios::end);auto length=in.tellg();
    if(length<0 || length>262144)throw std::runtime_error("Invalid launcher catalog size");in.seekg(0,std::ios::beg);
    Catalog c;c.settings.enable_audio=1;c.settings.audio_freq=48000;c.settings.volume=100;c.settings.window_scale=2;
    std::string line;size_t bytes=0;bool header=false,end=false;
    while(std::getline(in,line)) {
        bytes+=line.size()+1;if(bytes>262144||line.size()>16384||end)throw std::runtime_error("Invalid launcher catalog bounds");
        auto t=fields(line);if(t.empty())throw std::runtime_error("Empty launcher record");
        if(!header){if(t!=std::vector<std::string>{"xlle-launcher","1"})throw std::runtime_error("Launcher schema mismatch");header=true;continue;}
        auto x=[&](size_t i){return unhex(t.at(i));};
        if(t[0]=="title"&&t.size()==3){c.title=x(1);c.region=x(2);}
        else if(t[0]=="identity"&&t.size()==2) {
            if(t[1].size()!=64||t[1].find_first_not_of("0123456789abcdef")!=std::string::npos||c.identities.size()>=32||
               std::find(c.identities.begin(),c.identities.end(),t[1])!=c.identities.end())throw std::runtime_error("Invalid XBE identity");
            c.identities.push_back(t[1]);
        } else if(t[0]=="game-id"&&t.size()==2&&!c.has_game_id) {
            if(t[1].size()!=8||t[1].find_first_not_of("0123456789abcdef")!=std::string::npos)
                throw std::runtime_error("Invalid launcher title ID");
            c.game_id=(uint32_t)std::stoul(t[1],nullptr,16);c.has_game_id=true;
        } else if(t[0]=="volume"&&t.size()==2) {
            if(t[1].empty()||t[1].find_first_not_of("0123456789")!=std::string::npos||t[1].size()>3)throw std::runtime_error("Invalid audio volume");
            c.settings.volume=std::stoi(t[1]);if(c.settings.volume>100)throw std::runtime_error("Invalid audio volume");
        } else if(t[0]=="skip"&&t.size()==2&&(t[1]=="0"||t[1]=="1")) {
            c.settings.skip_launcher=t[1]=="1";
        } else if(t[0]=="player"&&t.size()==3) {
            c.player_source=t[1];c.player_guid=x(2);
            if(c.player_source!="auto"&&c.player_source!="none"&&c.player_source!="keyboard"&&c.player_source!="gamepad")throw std::runtime_error("Invalid Player 1 source");
            if(c.player_source=="gamepad"?(c.player_guid.size()!=32||c.player_guid.find_first_not_of("0123456789abcdef")!=std::string::npos):!c.player_guid.empty())throw std::runtime_error("Invalid Player 1 controller identity");
            c.settings.player_src[0]=c.player_source=="keyboard"?1:c.player_source=="gamepad"?2:0;
            copy(c.settings.player_gamepad_guid[0],c.player_guid);
        } else if(t[0]=="setup"&&t.size()==3&&(t[1]=="0"||t[1]=="1")&&!c.setup) {
            c.setup=true;c.needs_setup=t[1]=="1";copy(c.settings.bios_path,x(2));
        } else if(t[0]=="console"&&(t.size()==6||t.size()==7)&&!c.console&&c.setup&&
                  (t[1]=="0"||t[1]=="1")&&(t[2]=="0"||t[2]=="1")) {
            c.console=true;c.console_ui.acknowledged=t[1]=="1";c.console_ui.locked=t[2]=="1";
            copy(c.console_ui.kernel,c.settings.bios_path);copy(c.console_ui.eeprom,x(3));
            copy(c.console_ui.storage,x(4));c.choice_path=x(5);
            if(t.size()==7)copy(c.console_ui.hdd,x(6));
        } else if(t[0]=="boot"&&t.size()==5&&c.console&&!c.boot&&(t[1]=="0"||t[1]=="1")) {
            c.boot=true;c.console_ui.boot_available=true;c.console_ui.boot_enabled=t[1]=="1";
            copy(c.console_ui.boot_kernel,x(2));copy(c.console_ui.boot_status,x(3));c.boot_choice_path=x(4);
        } else if(t[0]=="image-import" && t.size()==2 && c.console && c.import_path.empty()) {
            c.import_path=x(1);c.console_ui.image_import=true;
        } else if(t[0]=="package"&&t.size()==5&&c.packages.size()<32) {
            Package p={x(1),x(2),x(3),x(4)};
            if(std::any_of(c.packages.begin(),c.packages.end(),[&](const Package &old){return old.id==p.id;}))throw std::runtime_error("Duplicate package");
            c.packages.push_back(p);
        } else if(t[0]=="feature"&&(t.size()==6||t.size()==7)&&c.features.size()<32) {
            Feature f;f.package=x(1);f.id=x(2);f.name=x(3);f.description=x(4);
            if(t[5]!="0"&&t[5]!="1")throw std::runtime_error("Invalid feature state");f.enabled=t[5]=="1";
            if(t.size()==7)f.group=x(6);
            if(f.group!="Other"&&f.group!="Controls"&&f.group!="Visual enhancements"&&f.group!="Gameplay"&&f.group!="Diagnostics"&&f.group!="Presentation")
                throw std::runtime_error("Invalid feature category");
            if(feature(c,f.package.c_str(),f.id.c_str())||std::none_of(c.packages.begin(),c.packages.end(),[&](const Package &p){return p.id==f.package;}))throw std::runtime_error("Invalid feature package");
            c.features.push_back(f);
        } else if(t[0]=="option"&&t.size()==7) {
            Feature *f=feature(c,x(1).c_str(),x(2).c_str());if(!f||f->options.size()>=16||option(f,x(3).c_str()))throw std::runtime_error("Invalid feature option");
            f->options.push_back({x(3),x(4),x(5),x(6),{}});
        } else if(t[0]=="choice"&&t.size()==6) {
            Option *o=option(feature(c,x(1).c_str(),x(2).c_str()),x(3).c_str());if(!o||o->choices.size()>=32)throw std::runtime_error("Invalid option choice");
            std::string value=x(4);if(std::any_of(o->choices.begin(),o->choices.end(),[&](const Choice &v){return v.value==value;}))throw std::runtime_error("Duplicate option choice");
            o->choices.push_back({value,x(5)});
        } else if(t[0]=="end"&&t.size()==1)end=true;
        else throw std::runtime_error("Unknown launcher record");
    }
    if(!end||c.title.empty()||c.identities.empty())throw std::runtime_error("Incomplete launcher catalog");
    for(auto &f:c.features)for(auto &o:f.options)if(o.choices.empty()||std::none_of(o.choices.begin(),o.choices.end(),[&](const Choice &v){return v.value==o.value;}))throw std::runtime_error("Unsupported persisted option");
    return c;
}
static int feature_count(void *p){auto &c=*(Catalog*)p;return int(c.features.size())+(c.console_ui.image_import?1:0);}
static int package_count(void *p){return int(((Catalog*)p)->packages.size());}
static int package_get(void *p,int index,RecompLauncherCModPackage *out) {
    auto &c=*(Catalog*)p;if(!out||index<0||size_t(index)>=c.packages.size())return 0;
    *out={};auto &v=c.packages[index];copy(out->id,v.id);copy(out->name,v.name);copy(out->version,v.version);copy(out->description,v.description);
    copy(out->status,"Bundled");for(auto &f:c.features)if(f.package==v.id&&f.enabled)out->enabled=1;return 1;
}
static int feature_get(void *p,int index,RecompLauncherCModFeature *out) {
    auto &c=*(Catalog*)p;if(!out||index<0)return 0;
    if(c.console_ui.image_import) {
        if(index==0) {
            *out={};copy(out->id,"manage");copy(out->package_id,"xboxllr.tools.graffiti");
            copy(out->name,"Manage Graffiti");copy(out->package_name,"Graffiti tools");copy(out->group,"Gameplay");
            copy(out->description,"Browse, view and delete saved graffiti, or add PNG, JPEG or TGA artwork as a new design. Larger images and sharper detail are supported. In game: Roboy > Graffiti > Pick > Edit icon > HDD slots.");
            out->enabled=1;return 1;
        }
        --index;
    }
    if(size_t(index)>=c.features.size())return 0;
    *out={};auto &f=c.features[index];copy(out->id,f.id);copy(out->package_id,f.package);copy(out->name,f.name);copy(out->description,f.description);
    for(auto &v:c.packages)if(v.id==f.package){copy(out->package_version,v.version);copy(out->package_name,v.name);}
    copy(out->group,f.group);out->enabled=f.enabled;out->option_count=int(f.options.size());return 1;
}
static int option_get(void *p,const char *pid,const char *fid,int index,RecompLauncherCModOption *out) {
    auto *f=feature(*(Catalog*)p,pid,fid);if(!f||!out||index<0||size_t(index)>=f->options.size())return 0;
    auto &o=f->options[index];*out={};copy(out->id,o.id);copy(out->label,o.label);copy(out->value,o.value);copy(out->default_value,o.initial);
    out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=int(o.choices.size());return 1;
}
static int submit_image(void *opaque,const char *game,const char *source,int size,int fit,int density,
                        const unsigned char *pixels,int width,int height) {
    auto &c=*(Catalog*)opaque;auto &ui=c.console_ui;
    try {
        if(!game || !*game)throw std::runtime_error("Choose your game before importing graffiti");
        if(ui.import_pending || (pixels && (width<=0 || height<=0 || width>8192 || height>8192 ||
           (uint64_t)width*height>16777216 || size<0 || size>4 || fit<0 || fit>2)))
            throw std::runtime_error("Invalid or pending graffiti import");
        auto folder=std::filesystem::u8path(c.import_path);
        if(pixels){std::ofstream out(folder/"pixels.rgba",std::ios::binary|std::ios::trunc);
        out.write((const char*)pixels,(std::streamsize)width*height*4);out.close();
        if(!out)throw std::runtime_error("Cannot stage the selected image");}
        std::ofstream request(folder/"request.tmp",std::ios::binary|std::ios::trunc);
        request<<"xboxllr-image-import 1\naction "<<(pixels?"install":"inspect")<<"\ngame "<<hex(game)<<"\n";
        if(pixels)request<<"source "<<hex(source)
               <<"\nsize "<<size<<"\nfit "<<fit<<"\ndensity "<<density<<"\nwidth "<<width<<"\nheight "<<height<<"\n";
        request<<"end\n";
        request.close();if(!request)throw std::runtime_error("Cannot stage image request");
        std::filesystem::rename(folder/"request.tmp",folder/"request");
        ui.import_pending=true;copy(ui.import_status,pixels?"Installing graffiti... Please wait.":"Checking available save space...");return 1;
    } catch(const std::exception &error){copy(ui.import_status,error.what());return 0;}
}
static int manage_image(void *opaque,const char *game,const char *action,int slot,const char *sha) {
    auto &c=*(Catalog*)opaque;
    try {
        if(c.console_ui.import_pending || !game || !*game || !sha || std::strlen(sha)!=64 ||
           std::strspn(sha,"0123456789abcdef")!=64 || slot<0 || slot>=28 ||
           (std::strcmp(action,"load") && std::strcmp(action,"delete")))throw std::runtime_error("Invalid saved design selection");
        auto folder=std::filesystem::u8path(c.import_path);
        std::ofstream out(folder/"request.tmp",std::ios::binary|std::ios::trunc);
        out<<"xboxllr-image-import 1\naction "<<action<<"\ngame "<<hex(game)<<"\nslot "<<slot<<"\nsha "<<sha<<"\nend\n";
        out.close();if(!out)throw std::runtime_error("Cannot stage saved design request");
        std::filesystem::rename(folder/"request.tmp",folder/"request");
        c.console_ui.import_pending=true;copy(c.console_ui.import_status,!std::strcmp(action,"load")?"Loading full-size artwork...":"Deleting selected design...");return 1;
    } catch(const std::exception &error){copy(c.console_ui.import_status,error.what());return 0;}
}
static std::vector<unsigned char> image_data(const std::filesystem::path &path,size_t limit) {
    auto n=std::filesystem::file_size(path);if(n>limit)throw std::runtime_error("Oversized saved artwork");
    std::vector<unsigned char> data((size_t)n);std::ifstream in(path,std::ios::binary);
    if(!in.read((char*)data.data(),data.size()))throw std::runtime_error("Incomplete saved artwork");return data;
}
static unsigned image_u32(const std::vector<unsigned char> &data,size_t at) {
    if(at+4>data.size())throw std::runtime_error("Incomplete saved artwork");
    return data[at]|(unsigned)data[at+1]<<8|(unsigned)data[at+2]<<16|(unsigned)data[at+3]<<24;
}
static std::string image_sha(const std::vector<unsigned char> &data,size_t at) {
    if(at+32>data.size())throw std::runtime_error("Incomplete saved artwork");
    std::string raw((const char*)data.data()+at,32);return hex(raw);
}
static void read_gallery(Catalog &c) {
    auto data=image_data(std::filesystem::u8path(c.import_path)/"gallery.bin",2*1024*1024);
    if(data.size()<12 || std::memcmp(data.data(),"XLLEGA1\0",8))throw std::runtime_error("Invalid saved design gallery");
    unsigned count=image_u32(data,8),seen=0;if(count>28)throw std::runtime_error("Invalid saved design count");
    std::vector<SavedImage> images;size_t at=12;
    for(unsigned i=0;i<count;i++) {
        SavedImage image;auto &d=image.info;
        d.slot=image_u32(data,at);d.size=image_u32(data,at+4);d.density=image_u32(data,at+8);
        d.width=image_u32(data,at+12);d.height=image_u32(data,at+16);d.thumb_width=image_u32(data,at+20);d.thumb_height=image_u32(data,at+24);
        unsigned flags=image_u32(data,at+28);copy(d.sha,image_sha(data,at+32));at+=64;
        if(d.slot<0 || d.slot>=28 || (seen&(1u<<d.slot)) || d.size<0 || d.size>4 ||
           (d.density!=1 && d.density!=2 && d.density!=4) || d.width<0 || d.width>4096 || d.height<0 || d.height>1024 ||
           d.thumb_width<0 || d.thumb_width>64 || d.thumb_height<0 || d.thumb_height>64 || flags>1 ||
           (!d.thumb_width!=!d.thumb_height))throw std::runtime_error("Invalid saved design metadata");
        seen|=1u<<d.slot;d.deletable=flags!=0;size_t bytes=(size_t)d.thumb_width*d.thumb_height*4;
        if(at+bytes>data.size())throw std::runtime_error("Incomplete saved design thumbnail");
        image.rgba.assign(data.begin()+at,data.begin()+at+bytes);at+=bytes;images.push_back(std::move(image));
    }
    if(at!=data.size())throw std::runtime_error("Invalid saved design gallery length");
    c.saved_images=std::move(images);c.console_ui.saved_count=(int)c.saved_images.size();
    c.console_ui.gallery_ready=true;++c.console_ui.gallery_version;
    c.preview_pixels.clear();c.console_ui.preview_slot=-1;++c.console_ui.preview_version;
}
static void read_preview(Catalog &c,int slot) {
    auto data=image_data(std::filesystem::u8path(c.import_path)/"preview.bin",52+16*1024*1024);
    if(data.size()<52 || std::memcmp(data.data(),"XLLEGP1\0",8) || image_u32(data,8)!=(unsigned)slot)throw std::runtime_error("Invalid saved artwork preview");
    unsigned w=image_u32(data,12),h=image_u32(data,16);auto sha=image_sha(data,20);
    auto selected=std::find_if(c.saved_images.begin(),c.saved_images.end(),[&](const SavedImage &image){return image.info.slot==slot && sha==image.info.sha;});
    if(selected==c.saved_images.end() || w<1 || w>4096 || h<1 || h>1024 || data.size()!=52+(size_t)w*h*4)throw std::runtime_error("Saved artwork changed. Refresh the gallery.");
    c.preview_pixels.assign(data.begin()+52,data.end());c.preview_width=w;c.preview_height=h;
    c.console_ui.preview_slot=slot;++c.console_ui.preview_version;
}
static int saved_image(void *opaque,int index,XboxllrGraffitiDesign *out) {
    auto &c=*(Catalog*)opaque;if(!out || index<0 || (size_t)index>=c.saved_images.size())return 0;
    auto &image=c.saved_images[index];*out=image.info;out->pixels=image.rgba.empty()?nullptr:image.rgba.data();return 1;
}
static const unsigned char *preview_image(void *opaque,int *w,int *h) {
    auto &c=*(Catalog*)opaque;*w=c.preview_width;*h=c.preview_height;return c.preview_pixels.empty()?nullptr:c.preview_pixels.data();
}
static void poll_image(void *opaque) {
    auto &c=*(Catalog*)opaque;if(!c.console_ui.import_pending)return;
    try {
        auto response=std::filesystem::u8path(c.import_path)/"response";
        if(!std::filesystem::exists(response))return;
        if(std::filesystem::file_size(response)>8192)throw std::runtime_error("Invalid image-import response");
        std::ifstream in(response,std::ios::binary);std::string line;std::getline(in,line);
        if(line!="xboxllr-image-result 1")throw std::runtime_error("Invalid image-import response");
        bool gallery=false;int preview=-1;
        while(std::getline(in,line) && line!="end") {
            auto t=fields(line);if(t.size()!=2)throw std::runtime_error("Invalid image result field");
            if(t[0]=="status")copy(c.console_ui.import_status,unhex(t[1]));
            else if(t[0]=="capacity")copy(c.console_ui.import_capacity,unhex(t[1]));
            else if(t[0]=="slots") {int n=std::stoi(t[1]);if(n< -1 || n>28)throw std::runtime_error("Invalid slot count");c.console_ui.free_slots=n;}
            else if(t[0]=="locked" && t[1]=="1")c.console_ui.locked=1;
            else if(t[0]=="gallery")gallery=t[1]=="1";
            else if(t[0]=="preview") {preview=std::stoi(t[1]);if(preview< -1 || preview>=28)throw std::runtime_error("Invalid preview slot");}
        }
        in.close();
        std::filesystem::remove(response);c.console_ui.import_pending=false;
        if(gallery)read_gallery(c);
        if(preview>=0)read_preview(c,preview);
    } catch(const std::exception &error){copy(c.console_ui.import_status,error.what());c.console_ui.import_pending=false;}
}
static int choice_get(void *p,const char *pid,const char *fid,const char *oid,int index,RecompLauncherCModChoice *out) {
    auto *o=option(feature(*(Catalog*)p,pid,fid),oid);if(!o||!out||index<0||size_t(index)>=o->choices.size())return 0;
    *out={};copy(out->value,o->choices[index].value);copy(out->label,o->choices[index].label);return 1;
}
static int feature_enable(void *p,const char *pid,const char *fid,int enabled) {
    // Management is an action tool, never a saved switch. Disable all cannot
    // disable it, and no runtime mod setting is generated for this entry.
    if(((Catalog*)p)->console_ui.image_import && !strcmp(pid,"xboxllr.tools.graffiti") && !strcmp(fid,"manage"))return 1;
    auto *f=feature(*(Catalog*)p,pid,fid);if(!f)return 0;f->enabled=enabled!=0;return 1;
}
static int package_enable(void *p,const char *pid,int enabled) {
    bool found=false;for(auto &f:((Catalog*)p)->features)if(f.package==pid){f.enabled=enabled!=0;found=true;}return found;
}
static int set_option(void *p,const char *pid,const char *fid,const char *oid,const char *value) {
    auto *o=option(feature(*(Catalog*)p,pid,fid),oid);if(!o||!value)return 0;
    for(auto &v:o->choices)if(v.value==value){o->value=value;return 1;}return 0;
}
static std::string value(Catalog &c,const char *pid,const char *fid,const char *oid,const char *off) {
    auto *f=feature(c,pid,fid);auto *o=option(f,oid);return f&&f->enabled&&o?o->value:off;
}
static int commit(void *p,const char *) {
    auto &c=*(Catalog*)p;c.error.clear();
    try {
        const char *v="xboxlle.enhancement.visuals";
        PilotVisualMods m;pilot_visual_mods_default(&m);
        m.internal_height=std::stoul(value(c,v,"resolution","height","0"));
        m.supersample=std::stoul(value(c,v,"supersampling","axis","1"));
        m.anisotropy=std::stoul(value(c,v,"filtering","anisotropy","1"));
        m.antialiasing=value(c,v,"antialiasing","method","original")=="fxaa";
        std::string aspect="4:3";
        /* The generic UI uses the common aspect feature ID for other titles.
         * No guest addresses or gameplay hooks belong in this adapter. */
        for(auto &f:c.features)if(f.enabled)for(auto &o:f.options)if(o.id=="aspect")aspect=o.value;
        if(aspect=="adaptive")aspect="16:9";
        unsigned n=0,d=0;char extra;if(std::sscanf(aspect.c_str(),"%u:%u%c",&n,&d,&extra)!=2)throw std::runtime_error("Invalid aspect");
        m.aspect_numerator=n;m.aspect_denominator=d;
        auto frames=value(c,"xboxlle.enhancement.frame-rate","frame-rate","mode","original");
        m.frame_mode=frames=="interpolated"?PILOT_FRAMES_INTERPOLATED:frames=="actual"?PILOT_FRAMES_ACTUAL:PILOT_FRAMES_ORIGINAL;
        m.frame_rate=m.frame_mode?std::stoul(value(c,"xboxlle.enhancement.frame-rate","frame-rate","rate","60")):0;
        uint32_t size[2];if(!pilot_visual_mods_size(&m,640,480,640,480,16384,size))throw std::runtime_error("Resolution, aspect and supersampling exceed the renderer limit. Choose a lower setting.");
        return 1;
    } catch(const std::exception &e){c.error=e.what();return 0;}
}
static const char *last_error(void *p){return ((Catalog*)p)->error.c_str();}
static void persist_console(void *context) {
    auto &c=*(Catalog*)context;auto &s=c.console_ui;
    std::string temporary=c.choice_path+".tmp";
    std::ofstream out(std::filesystem::u8path(temporary),std::ios::binary|std::ios::trunc);
    out<<"xboxllr-console-choice 2\nacknowledged "<<s.acknowledged<<"\nkernel "<<hex(s.kernel)
       <<"\neeprom "<<hex(s.eeprom)<<"\nstorage "<<hex(s.storage)<<"\nhdd "<<hex(s.hdd)<<"\nend\n";
    out.close();
    if(!out)throw std::runtime_error("Could not save console setup choices");
#if defined(_WIN32)
    if(!MoveFileExW(std::filesystem::u8path(temporary).c_str(),std::filesystem::u8path(c.choice_path).c_str(),
                    MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not store console setup choices");
#else
    std::filesystem::rename(std::filesystem::u8path(temporary),std::filesystem::u8path(c.choice_path));
#endif
}
/* The producer checks the exact kernel fingerprint before publishing keys. */
static void persist_boot(void *context) {
    auto &c=*(Catalog*)context;auto &s=c.console_ui;
    try {
        if(c.boot_choice_path.empty())throw std::runtime_error("Startup settings path unavailable");
        const auto path=std::filesystem::u8path(c.boot_choice_path);
        const auto temporary=std::filesystem::u8path(c.boot_choice_path+".tmp");
        for(auto p=path.parent_path();!p.empty();p=p.parent_path()) {
            if(std::filesystem::is_symlink(std::filesystem::symlink_status(p)))throw std::runtime_error("Linked startup path");
#if defined(_WIN32)
            const DWORD attributes=GetFileAttributesW(p.c_str());
            if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked startup path");
#endif
            if(p==p.parent_path())break;
        }
        if(std::filesystem::exists(temporary)||std::filesystem::is_symlink(std::filesystem::symlink_status(temporary)))throw std::runtime_error("Startup settings temporary path occupied");
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        out<<"xboxllr-boot-choice 1\nenabled "<<(s.boot_enabled?1:0)<<"\nkernel "<<hex(s.boot_kernel)<<"\nend\n";
        out.close();if(!out)throw std::runtime_error("Cannot save startup settings");
#if defined(_WIN32)
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot store startup settings");
#else
        std::filesystem::rename(temporary,path);
#endif
        copy(s.boot_status,"Saved. Startup data is checked when you launch.");
    } catch(const std::exception &) {copy(s.boot_status,"Startup settings could not be saved. Your console identity and saves are unchanged.");}
}
static int signing_source_verify(const char *path,RecompLauncherCBiosVerify *out) {
    if(!out)return 0;*out={};
    std::ifstream in(path?path:"",std::ios::binary|std::ios::ate);
    if(in&&in.tellg()>0&&in.tellg()<=1048576) {
        out->ok=1;out->warn=1;
        copy(out->detail,"Selected; exact source identity is checked when setting up saves.");
    } else copy(out->detail,"Choose your kernel dump for this preview's save-signing setup.");
    return 1;
}
static void save(const Catalog &c,const char *path,int result,const char *image) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot save launcher result");
    out<<"xlle-launcher-result 1\nresult "<<result<<"\nimage "<<hex(image)<<"\nvolume "<<(c.settings.enable_audio?c.settings.volume:0)<<"\nskip "<<(c.settings.skip_launcher?1:0)<<"\n";
    const int source=c.settings.player_src[0];
    const std::string kind=xboxllr_controller_automatic()&&source==0?"auto":source==2?"gamepad":source==1?"keyboard":"none";
    out<<"player "<<kind<<" "<<hex(source==2?c.settings.player_gamepad_guid[0]:"")<<"\n";
    if(c.setup)out<<"kernel "<<hex(c.console?c.console_ui.kernel:c.settings.bios_path)<<"\n";
    if(c.console)out<<"console "<<c.console_ui.acknowledged<<" "<<hex(c.console_ui.eeprom)<<" "<<hex(c.console_ui.storage)<<" "<<hex(c.console_ui.hdd)<<"\n";
    if(c.boot)out<<"boot "<<(c.console_ui.boot_enabled?1:0)<<" "<<hex(c.console_ui.boot_kernel)<<"\n";
    for(auto &f:c.features) {
        out<<"feature "<<hex(f.package)<<" "<<hex(f.id)<<" "<<f.enabled<<"\n";
        for(auto &o:f.options)out<<"option "<<hex(f.package)<<" "<<hex(f.id)<<" "<<hex(o.id)<<" "<<hex(o.value)<<"\n";
    }
    out<<"end\n";out.close();if(!out)throw std::runtime_error("Launcher result write failed");
}
int main(int argc,char **argv) {
    try {
        if(argc!=4&&argc!=5)throw std::runtime_error("Usage: xlle_launcher catalog result owned.xbe [--validate|--validate-xbe]");
        Catalog c=read_catalog(argv[1]);
        xboxllr_controller_begin(argv[1],c.player_source=="auto");
        xboxllr_expected_title_id=c.game_id;xboxllr_has_expected_title_id=c.has_game_id;
        if(c.console){c.console_ui.persist=persist_console;c.console_ui.persist_boot=persist_boot;c.console_ui.context=&c;c.console_ui.submit_image=submit_image;c.console_ui.manage_image=manage_image;c.console_ui.saved_image=saved_image;c.console_ui.preview_image=preview_image;c.console_ui.poll_image=poll_image;xboxllr_console_setup=&c.console_ui;}
        if(argc==5&&std::string(argv[4])=="--validate") {
            if(!commit(&c,argv[3]))throw std::runtime_error(c.error);
            for(int i=0;i<feature_count(&c);++i) {
                RecompLauncherCModFeature f={};feature_get(&c,i,&f);
                std::printf("[LAUNCHER MOD] %s\t%s\t%s\n",f.package_id,f.id,f.group);
            }
            save(c,argv[2],0,argv[3]);return 0;
        }
        if(argc==5&&std::string(argv[4])!="--validate-xbe")throw std::runtime_error("Unknown launcher check");
        RecompLauncherCModProvider mods={};mods.ctx=&c;mods.package_count=package_count;mods.package_get=package_get;mods.set_enabled=package_enable;
        mods.feature_count=feature_count;mods.feature_get=feature_get;mods.feature_option_get=option_get;mods.feature_choice_get=choice_get;
        mods.feature_enable=feature_enable;mods.feature_set_option=set_option;mods.commit=commit;mods.last_error=last_error;
        RecompLauncherCGameInfo game={};launcher_profile_apply("xbox",&game);game.name=c.title.c_str();game.region=c.region.c_str();game.mods=&mods;
        game.num_players=1;game.lock_device=0;game.hide_rebind=1;game.host_persists_paths=1;
        if(c.console)game.rom_noun="game files";
        if(c.setup&&!c.console) {
            game.setup_wizard_supported=1;game.needs_setup=c.needs_setup;
            game.has_bios=c.needs_setup?1:0;game.bios_verify=signing_source_verify;
            game.bios_name="Kernel dump (preview save setup)";
            static const char *patterns[]={"*.bin"};
            game.bios_patterns=patterns;game.num_bios_patterns=1;
            game.bios_filter_desc="Your Xbox kernel dump";
            game.rom_noun="Xbox executable";
        }
        if(std::ifstream("assets/img/boxart.png",std::ios::binary).good())
            game.boxart_path="assets/img/boxart.png";
        uint8_t sha[32][32];
        for(size_t n=0;n<c.identities.size();++n)for(unsigned i=0;i<32;++i)
            sha[n][i]=(uint8_t)std::stoul(c.identities[n].substr(i*2,2),nullptr,16);
        game.known_sha256=sha;game.num_known_sha256=(int)c.identities.size();
        if(argc==5) {
            // Exercise the real launcher fingerprint gate without a window or game.
            auto model=std::make_unique<LauncherModel>();
            launcher_model_init(model.get(),&c.settings,&game,argv[3]);
            if(!launcher_model_rom_verified(model.get()))throw std::runtime_error(xboxllr_xbe_rejection());
            save(c,argv[2],0,argv[3]);return 0;
        }
        char image[4096]={};int result=recomp_launcher_run_window(c.title.c_str(),&c.settings,&game,"assets",argv[3],image,sizeof(image));
        save(c,argv[2],result,image);return result==RECOMP_LAUNCHER_RESULT_UNAVAILABLE?2:0;
    } catch(const std::exception &e){std::fprintf(stderr,"[XBOX LAUNCHER] %s\n",e.what());return 2;}
}

/* Bundled catalog adapter for recomp-ui. It stages data, never loads mod code.
 * The game's Python resolver remains the final package/identity authority.
 * Hex UTF-8 fields keep the handoff bounded and independent of shell quoting. */
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "visual_mods.h"
#include "console_setup.h"
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
#if defined(_WIN32)
#include <windows.h>
#endif
XboxllrConsoleSetup *xboxllr_console_setup=nullptr;
struct Choice {std::string value,label;};
struct Option {std::string id,label,value,initial;std::vector<Choice> choices;};
struct Feature {std::string package,id,name,description;int enabled=0;std::vector<Option> options;};
struct Package {std::string id,name,version,description;};
struct Catalog {
    std::string title,region,sha,error;std::vector<Package> packages;std::vector<Feature> features;
    std::string player_source="auto",player_guid;
    RecompLauncherCSettings settings={};
    bool setup=false,needs_setup=false;
    bool console=false;
    std::string choice_path;
    XboxllrConsoleSetup console_ui;
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
            c.sha=t[1];if(c.sha.size()!=64||c.sha.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid XBE identity");
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
        } else if(t[0]=="package"&&t.size()==5&&c.packages.size()<32) {
            Package p={x(1),x(2),x(3),x(4)};
            if(std::any_of(c.packages.begin(),c.packages.end(),[&](const Package &old){return old.id==p.id;}))throw std::runtime_error("Duplicate package");
            c.packages.push_back(p);
        } else if(t[0]=="feature"&&t.size()==6&&c.features.size()<32) {
            Feature f;f.package=x(1);f.id=x(2);f.name=x(3);f.description=x(4);
            if(t[5]!="0"&&t[5]!="1")throw std::runtime_error("Invalid feature state");f.enabled=t[5]=="1";
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
    if(!end||c.title.empty()||c.sha.empty())throw std::runtime_error("Incomplete launcher catalog");
    for(auto &f:c.features)for(auto &o:f.options)if(o.choices.empty()||std::none_of(o.choices.begin(),o.choices.end(),[&](const Choice &v){return v.value==o.value;}))throw std::runtime_error("Unsupported persisted option");
    return c;
}
static int feature_count(void *p){return int(((Catalog*)p)->features.size());}
static int package_count(void *p){return int(((Catalog*)p)->packages.size());}
static int package_get(void *p,int index,RecompLauncherCModPackage *out) {
    auto &c=*(Catalog*)p;if(!out||index<0||size_t(index)>=c.packages.size())return 0;
    *out={};auto &v=c.packages[index];copy(out->id,v.id);copy(out->name,v.name);copy(out->version,v.version);copy(out->description,v.description);
    copy(out->status,"Bundled");for(auto &f:c.features)if(f.package==v.id&&f.enabled)out->enabled=1;return 1;
}
static int feature_get(void *p,int index,RecompLauncherCModFeature *out) {
    auto &c=*(Catalog*)p;if(!out||index<0||size_t(index)>=c.features.size())return 0;
    *out={};auto &f=c.features[index];copy(out->id,f.id);copy(out->package_id,f.package);copy(out->name,f.name);copy(out->description,f.description);
    for(auto &v:c.packages)if(v.id==f.package){copy(out->package_version,v.version);copy(out->package_name,v.name);}
    copy(out->group,"Visual enhancements");out->enabled=f.enabled;out->option_count=int(f.options.size());return 1;
}
static int option_get(void *p,const char *pid,const char *fid,int index,RecompLauncherCModOption *out) {
    auto *f=feature(*(Catalog*)p,pid,fid);if(!f||!out||index<0||size_t(index)>=f->options.size())return 0;
    auto &o=f->options[index];*out={};copy(out->id,o.id);copy(out->label,o.label);copy(out->value,o.value);copy(out->default_value,o.initial);
    out->type=RECOMP_MOD_OPTION_CHOICE;out->choice_count=int(o.choices.size());return 1;
}
static int choice_get(void *p,const char *pid,const char *fid,const char *oid,int index,RecompLauncherCModChoice *out) {
    auto *o=option(feature(*(Catalog*)p,pid,fid),oid);if(!o||!out||index<0||size_t(index)>=o->choices.size())return 0;
    *out={};copy(out->value,o->choices[index].value);copy(out->label,o->choices[index].label);return 1;
}
static int feature_enable(void *p,const char *pid,const char *fid,int enabled) {
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
    const std::string kind=c.player_source=="auto"&&source==0?"auto":source==2?"gamepad":source==1?"keyboard":"none";
    out<<"player "<<kind<<" "<<hex(source==2?c.settings.player_gamepad_guid[0]:"")<<"\n";
    if(c.setup)out<<"kernel "<<hex(c.console?c.console_ui.kernel:c.settings.bios_path)<<"\n";
    if(c.console)out<<"console "<<c.console_ui.acknowledged<<" "<<hex(c.console_ui.eeprom)<<" "<<hex(c.console_ui.storage)<<" "<<hex(c.console_ui.hdd)<<"\n";
    for(auto &f:c.features) {
        out<<"feature "<<hex(f.package)<<" "<<hex(f.id)<<" "<<f.enabled<<"\n";
        for(auto &o:f.options)out<<"option "<<hex(f.package)<<" "<<hex(f.id)<<" "<<hex(o.id)<<" "<<hex(o.value)<<"\n";
    }
    out<<"end\n";out.close();if(!out)throw std::runtime_error("Launcher result write failed");
}
int main(int argc,char **argv) {
    try {
        if(argc!=4&&argc!=5)throw std::runtime_error("Usage: xlle_launcher catalog result owned.xbe [--validate]");
        Catalog c=read_catalog(argv[1]);
        if(c.console){c.console_ui.persist=persist_console;c.console_ui.context=&c;xboxllr_console_setup=&c.console_ui;}
        if(argc==5){if(std::string(argv[4])!="--validate"||!commit(&c,argv[3]))throw std::runtime_error(c.error);save(c,argv[2],0,argv[3]);return 0;}
        /* Only first use chooses a default. Saved/disconnected pads retain
         * their GUID and the shared UI's normal hot-plug/offline behavior. */
        if(c.player_source=="auto") {
            c.settings.player_src[0]=1;
            if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0) {
                for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)) {
                    SDL_JoystickGUID guid=SDL_JoystickGetDeviceGUID(i);
                    SDL_JoystickGetGUIDString(guid,c.settings.player_gamepad_guid[0],sizeof(c.settings.player_gamepad_guid[0]));
                    c.settings.player_src[0]=2;break;
                }
            }
            c.player_source=c.settings.player_src[0]==2?"gamepad":"keyboard";
        }
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
        uint8_t sha[1][32];for(unsigned i=0;i<32;++i)sha[0][i]=(uint8_t)std::stoul(c.sha.substr(i*2,2),nullptr,16);
        game.known_sha256=sha;game.num_known_sha256=1;
        char image[4096]={};int result=recomp_launcher_run_window(c.title.c_str(),&c.settings,&game,"assets",argv[3],image,sizeof(image));
        save(c,argv[2],result,image);return result==RECOMP_LAUNCHER_RESULT_UNAVAILABLE?2:0;
    } catch(const std::exception &e){std::fprintf(stderr,"[XBOX LAUNCHER] %s\n",e.what());return 2;}
}

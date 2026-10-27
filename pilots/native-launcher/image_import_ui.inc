/* Title-declared management tool in the Mods list. It is always available:
 * no persisted enable bit, no game patch and no enable/disable checkbox. */
static bool xboxllr_is_image_tool(const RecompLauncherCModFeature &feature) {
    return xboxllr_console_setup && xboxllr_console_setup->image_import &&
        !std::strcmp(feature.package_id,"xboxllr.tools.graffiti") && !std::strcmp(feature.id,"manage");
}
static void xboxllr_artwork_image(LauncherTexture texture,float w,float h) {
    ImVec2 start=ImGui::GetCursorScreenPos();auto *draw=ImGui::GetWindowDrawList();
    for(float y=0;y<h;y+=px(12))for(float x=0;x<w;x+=px(12)) {
        bool odd=((int)(x/px(12))+(int)(y/px(12)))&1;
        draw->AddRectFilled(ImVec2(start.x+x,start.y+y),ImVec2(start.x+std::min(x+px(12),w),start.y+std::min(y+px(12),h)),odd?IM_COL32(90,90,90,255):IM_COL32(130,130,130,255));
    }
    draw->AddImage(tid(texture),start,ImVec2(start.x+w,start.y+h));ImGui::Dummy(ImVec2(w,h));
}
static void draw_xboxllr_saved_graffiti(const char *game,const LauncherTheme &th) {
    auto *s=xboxllr_console_setup;
    static unsigned version=~0u,preview_version=0;
    static std::vector<LauncherTexture> textures;
    static LauncherTexture full={};
    static int selected=-1,delete_slot=-1,full_width=0,full_height=0;
    static char delete_sha[65]={};
    static bool actual_size=false;
    if(version!=s->gallery_version || !s->gallery_ready) {
        for(auto &texture:textures)launcher_texture_free(&texture);textures.clear();selected=-1;
        if(s->gallery_ready)for(int i=0;i<s->saved_count;i++) {
            XboxllrGraffitiDesign d;s->saved_image(s->context,i,&d);
            textures.push_back(d.pixels?launcher_texture_rgba(d.pixels,d.thumb_width,d.thumb_height):LauncherTexture{});
        }
        version=s->gallery_version;
    }
    if(preview_version!=s->preview_version) {
        preview_version=s->preview_version;launcher_texture_free(&full);
        if(s->preview_slot>=0) {
            auto *pixels=s->preview_image(s->context,&full_width,&full_height);
            if(pixels) {full=launcher_texture_rgba(pixels,full_width,full_height);actual_size=false;ImGui::OpenPopup("Saved graffiti artwork");}
        }
    }
    ImGui::BeginDisabled(s->import_pending || !game || !*game);
    if(ImGui::Button("Refresh saved designs"))s->refresh_capacity=true;
    ImGui::EndDisabled();
    if(!s->gallery_ready)ImGui::TextWrapped("Reading saved designs...");
    else if(!s->saved_count)ImGui::TextWrapped("No graffiti designs are saved on this console yet. Add artwork in Import image.");
    else {
        const float cell=px(118),thumb=px(72);int columns=std::max(1,(int)(ImGui::GetContentRegionAvail().x/cell));
        ImGui::BeginDisabled(s->import_pending || s->refresh_capacity);
        for(int i=0;i<s->saved_count;i++) {
            XboxllrGraffitiDesign d;if(!s->saved_image(s->context,i,&d))continue;
            ImGui::PushID(i);ImGui::BeginGroup();
            ImVec2 start=ImGui::GetCursorScreenPos();
            if(ImGui::InvisibleButton("Design",ImVec2(cell-px(8),thumb+px(32))))selected=i;
            auto *draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(start,ImVec2(start.x+cell-px(8),start.y+thumb+px(32)),selected==i?IM_COL32(70,95,35,255):IM_COL32(35,38,35,255),px(4));
            if(textures[i].id) {
                float w=thumb,h=thumb;if(d.thumb_width>d.thumb_height)h=w*d.thumb_height/d.thumb_width;else w=h*d.thumb_width/d.thumb_height;
                ImVec2 lo(start.x+(cell-px(8)-w)*.5f,start.y+(thumb-h)*.5f+px(4));
                draw->AddImage(tid(textures[i]),lo,ImVec2(lo.x+w,lo.y+h));
            } else draw->AddText(ImVec2(start.x+px(8),start.y+px(24)),IM_COL32(180,180,180,255),"Unavailable");
            char label[32];std::snprintf(label,sizeof(label),"GRAFFITI%03d",d.slot+1);
            draw->AddText(ImVec2(start.x+px(5),start.y+thumb+px(8)),ImGui::GetColorU32(ImGuiCol_Text),label);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s / %dx detail\nClick to select",label,d.width?"Saved artwork":"Preview unavailable",d.density);
            ImGui::EndGroup();ImGui::PopID();if((i+1)%columns && i+1<s->saved_count)ImGui::SameLine(0,px(8));
        }
        if(selected>=0 && selected<s->saved_count) {
            XboxllrGraffitiDesign d;s->saved_image(s->context,selected,&d);
            const char *names[]={"SS","S","M","L","XL"};
            ImGui::Spacing();ImGui::Text("GRAFFITI%03d | %s | %dx detail",d.slot+1,names[d.size],d.density);
            ImGui::BeginDisabled(!d.pixels);
            if(ImGui::Button("View full size"))s->manage_image(s->context,game,"load",d.slot,d.sha);
            ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!d.deletable);
            if(ImGui::Button("Delete selected design")) {
                delete_slot=d.slot;std::snprintf(delete_sha,sizeof(delete_sha),"%s",d.sha);ImGui::OpenPopup("Delete saved graffiti?");
            }
            ImGui::EndDisabled();
        } else ImGui::TextColored(col(th.text_muted),"Select a design to view its full-size artwork or delete it.");
        ImGui::EndDisabled();
    }
    if(ImGui::BeginPopupModal("Delete saved graffiti?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete GRAFFITI%03d from this console?",delete_slot+1);
        ImGui::TextWrapped("This removes the selected design and its high-resolution artwork, freeing a graffiti slot.");
        if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::SameLine();
        if(ImGui::Button("Delete design")) {s->manage_image(s->context,game,"delete",delete_slot,delete_sha);ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    ImVec2 display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(display.x*.9f,px(1100)),std::min(display.y*.9f,px(850))),ImGuiCond_Appearing);
    bool open=true;
    if(ImGui::BeginPopupModal("Saved graffiti artwork",&open)) {
        ImGui::Text("GRAFFITI%03d | %d x %d",s->preview_slot+1,full_width,full_height);
        ImGui::Checkbox("Actual size (scroll to inspect)",&actual_size);ImGui::SameLine();
        if(ImGui::Button("Close"))ImGui::CloseCurrentPopup();
        if(full.id && ImGui::BeginChild("Artwork canvas",ImVec2(0,0),false,ImGuiWindowFlags_HorizontalScrollbar)) {
            float w=(float)full_width,h=(float)full_height;
            if(!actual_size) {auto space=ImGui::GetContentRegionAvail();float scale=std::min(space.x/w,space.y/h);w*=scale;h*=scale;}
            xboxllr_artwork_image(full,w,h);
        }
        if(full.id)ImGui::EndChild();
        ImGui::EndPopup();
    }
}
static void draw_xboxllr_image_import(LauncherModel *m,const LauncherTheme &th) {
    auto *s=xboxllr_console_setup;if(!s || !s->image_import)return;
    static char source[4096]={},error[512]={};
    static unsigned char *pixels=nullptr;
    static int width=0,height=0,size=0,fit=0,detail=2;
    static LauncherTexture preview={};
    if(s->poll_image)s->poll_image(s->context);
    const char *game=launcher_model_rom_path(m);
    static std::string capacity_context;
    std::string context=std::string(game?game:"")+"\n"+s->storage+"\n"+s->hdd;
    if(context!=capacity_context){s->refresh_capacity=true;s->gallery_ready=false;s->free_slots=-1;capacity_context=context;}
    if(s->refresh_capacity && !s->import_pending && game && *game && s->submit_image) {
        xboxllr_setup_persist();s->import_capacity[0]=0;
        if(s->submit_image(s->context,game,"",0,0,1,nullptr,0,0))s->refresh_capacity=false;
    }
    if(!game || !*game)ImGui::TextWrapped("Choose your game on the main screen to see available graffiti slots and save space.");
    else if(s->import_capacity[0])ImGui::TextColored(col(th.accent),"%s",s->import_capacity);
    else ImGui::TextColored(col(th.text_muted),"%s",s->import_pending?"Checking free slots and save space...":"Save space could not be checked.");
    ImGui::Spacing();
    static bool importing=false;
    if(ImGui::Button("Saved designs"))importing=false;
    ImGui::SameLine();if(ImGui::Button("Import image"))importing=true;
    ImGui::Separator();
    if(!importing)draw_xboxllr_saved_graffiti(game,th);
    else {
    ImGui::TextWrapped("Larger images are accepted: up to 8192 pixels per side and 16 million pixels total. Choose a graffiti shape below; 2x or 4x detail keeps more image detail without changing its size on a wall.");
    ImGui::BeginDisabled(s->import_pending);
    if(ImGui::Button("Choose PNG or JPEG")) {
        PickRequest req;req.title="Choose graffiti artwork";req.start_path=source;req.in_app_only=true;
        if(!source[0]) {
            std::error_code ec;
            auto samples=std::filesystem::current_path()/".."/"graffiti-artwork";
            if(std::filesystem::is_directory(samples,ec))req.start_path=samples.u8string();
            else if(const char *user=std::getenv("USERPROFILE")) {
                auto pictures=std::filesystem::u8path(user)/"Pictures";ec.clear();
                req.start_path=std::filesystem::is_directory(pictures,ec)?pictures.u8string():user;
            }
        }
        req.mode=PickMode::OpenFile;req.patterns={"*.png","*.jpg","*.jpeg","*.tga"};req.description="Graffiti image";
        req.on_pick=[](const char *path){
            if(!path || !*path)return;
            int w=0,h=0;unsigned char *loaded=launcher_image_load_bounded_rgba(path,&w,&h);
            if(!loaded){std::snprintf(error,sizeof(error),"Cannot read image. Use PNG/JPEG/TGA, at most 16 million pixels and 64 MB.");return;}
            launcher_image_free(pixels);pixels=loaded;width=w;height=h;
            launcher_texture_free(&preview);preview=launcher_texture_rgba(pixels,width,height);
            std::snprintf(source,sizeof(source),"%s",path);error[0]=0;
        };
        // Keep the SDL/GL event loop alive: no blocking desktop dialog.
        ui_pick(m,std::move(req));
    }
    if(source[0])ImGui::TextWrapped("%s (%d x %d)",source,width,height);
    const char *sizes[]={"SS | 1:1 (orig: 128x128)","S | 1:1 (orig: 128x128)","M | 3:2 (orig: 384x256)","L | 2:1 (orig: 512x256)","XL | 4:1 (orig: 1024x256)"};
    const char *fits[]={"Fit whole image (transparent margins)","Fill and crop","Stretch to size"};
    ImGui::SetNextItemWidth(std::min(px(480),ImGui::GetContentRegionAvail().x*.75f));
    ImGui::Combo("Graffiti size",&size,sizes,5);
    ImGui::SetNextItemWidth(std::min(px(480),ImGui::GetContentRegionAvail().x*.75f));
    ImGui::Combo("Image fit",&fit,fits,3);
    const char *details[]={"Native detail","2x detail","4x detail"};
    const int densities[]={1,2,4},widths[]={128,128,384,512,1024},heights[]={128,128,256,256,256};
    ImGui::SetNextItemWidth(std::min(px(480),ImGui::GetContentRegionAvail().x*.75f));
    ImGui::Combo("Image detail",&detail,details,3);
    const int density=densities[detail];
    const char *size_names[]={"SS","S","M","L","XL"};
    ImGui::Text("Artwork: %d x %d. Wall coverage stays %s.",widths[size]*density,heights[size]*density,size_names[size]);
    const unsigned estimates[]={121762,121762,925602,926626,1848226};
    double estimate=estimates[size]+(density>1?88.0+widths[size]*heights[size]*density*density*4.0:0);
    ImGui::Text("New design: about %.0f KiB. Existing designs are never replaced.",estimate/1024.0);
    if(preview.id) {
        const float ratios[]={1,1,1.5f,2,4};
        float box_w=std::min(ImGui::GetContentRegionAvail().x,px(520)),box_h=box_w/ratios[size];
        if(box_h>px(260)){box_h=px(260);box_w=box_h*ratios[size];}
        ImVec2 start=ImGui::GetCursorScreenPos();auto *draw=ImGui::GetWindowDrawList();
        for(float y=0;y<box_h;y+=px(12))for(float x=0;x<box_w;x+=px(12)) {
            bool odd=((int)(x/px(12))+(int)(y/px(12)))&1;
            draw->AddRectFilled(ImVec2(start.x+x,start.y+y),ImVec2(start.x+std::min(x+px(12),box_w),start.y+std::min(y+px(12),box_h)),odd?IM_COL32(90,90,90,255):IM_COL32(130,130,130,255));
        }
        ImVec2 low=start,high(start.x+box_w,start.y+box_h),uv0(0,0),uv1(1,1);
        float source_ratio=(float)width/height,target_ratio=box_w/box_h;
        if(fit==0) {
            if(source_ratio>target_ratio){float h=box_w/source_ratio;low.y+=(box_h-h)*.5f;high.y=low.y+h;}
            else{float w=box_h*source_ratio;low.x+=(box_w-w)*.5f;high.x=low.x+w;}
        } else if(fit==1) {
            if(source_ratio>target_ratio){float fraction=target_ratio/source_ratio;uv0.x=(1-fraction)*.5f;uv1.x=1-uv0.x;}
            else{float fraction=source_ratio/target_ratio;uv0.y=(1-fraction)*.5f;uv1.y=1-uv0.y;}
        }
        draw->AddImage(tid(preview),low,high,uv0,uv1);ImGui::Dummy(ImVec2(box_w,box_h));
        ImGui::TextWrapped("Higher detail keeps a native save plus sharper artwork for this build. Transparency uses the game's cutout mask. Editing and saving in the original editor returns that design to native detail.");
    }
    ImGui::BeginDisabled(s->free_slots==0 || !pixels || !launcher_model_rom_path(m) || !launcher_model_rom_path(m)[0]);
    if(ImGui::Button("Install as new graffiti design") && s->submit_image) {
        xboxllr_setup_persist();
        s->submit_image(s->context,launcher_model_rom_path(m),source,size,fit,density,pixels,width,height);
    }
    ImGui::EndDisabled();ImGui::EndDisabled();
    if(error[0])ImGui::TextWrapped("%s",error);
    }
    if(s->import_status[0])ImGui::TextColored(col(th.accent),"%s",s->import_status);
}

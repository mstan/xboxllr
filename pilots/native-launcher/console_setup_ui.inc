/* Included by the pinned frontend adapter after its common file picker.
 * Uses the same ImGui theme, navigation and file browser as recomp-ui. */
static void xboxllr_setup_persist() {
    auto *s=xboxllr_console_setup;
    if(s && s->persist)s->persist(s->context);
}
static void xboxllr_pick_console_file(LauncherModel *m,const char *title,char *target,int kind=0,char *alternative=nullptr) {
    PickRequest req;
    req.title=title;req.start_path=target;
    req.mode=kind==1?PickMode::Folder:PickMode::OpenFile;
    if(kind==2){req.patterns={"*.qcow2","*.img","*.raw","*.bin"};req.description="Xbox HDD image";}
    else if(!kind){req.patterns={"*.bin"};req.description="Xbox dump (.bin)";}
    req.on_pick=[target,alternative](const char *path){if(path && *path){
        std::snprintf(target,4096,"%s",path);if(alternative)alternative[0]=0;
    }};
    ui_pick(m,std::move(req));
}
static void xboxllr_console_path(LauncherModel *m,const char *label,const char *picker,char *target,int kind=0,char *alternative=nullptr) {
    ImGui::PushID(label);
    const float start=ImGui::GetCursorPosX();
    const bool wide=ImGui::GetContentRegionAvail().x>px(600);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if(wide)ImGui::SameLine(start+px(230));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-px(148));
    if(ImGui::InputTextWithHint("##path",kind==1?"Automatic - private to this game":"Not provided",target,4096) && target[0] && alternative)
        alternative[0]=0;
    ImGui::SameLine();
    if(ImGui::Button("Browse",ImVec2(px(82),0)))xboxllr_pick_console_file(m,picker,target,kind,alternative);
    ImGui::SameLine();
    ImGui::BeginDisabled(!target[0]);
    if(ImGui::Button("Clear",ImVec2(px(46),0)))target[0]=0;
    ImGui::EndDisabled();
    ImGui::PopID();
}
static void draw_xboxllr_console_controls(LauncherModel *m,const LauncherTheme &th) {
    auto *s=xboxllr_console_setup;if(!s)return;
    eyebrow("CONSOLE SETUP (OPTIONAL)");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(col(th.text_muted),
        "Use defaults for new local saves. Add original Xbox files only if you need your original console's save identity.");
    if(s->locked)ImGui::TextColored(col(th.text_muted),
        "This setup already has a save identity. It is kept unchanged to protect your progress. "
        "Use a separate fresh setup for a different console or storage.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0,px(6)));
    if(s->locked)ImGui::BeginDisabled();
    xboxllr_console_path(m,"Kernel dump","Select your original Xbox kernel dump",s->kernel);
    ImGui::TextWrapped("Helps use original Xbox saves. The kernel does not run during play.");
    ImGui::Dummy(ImVec2(0,px(6)));
    xboxllr_console_path(m,"EEPROM (console identity)","Select your original Xbox EEPROM dump",s->eeprom);
    ImGui::TextWrapped("Use with the kernel dump when a save needs your original console identity.");
    ImGui::Dummy(ImVec2(0,px(8)));ImGui::Separator();ImGui::Dummy(ImVec2(0,px(8)));
    ImGui::BeginDisabled(s->storage[0] && !s->hdd[0]);
    xboxllr_console_path(m,"HDD image","Select an existing Xbox HDD image (QCOW2 or raw)",s->hdd,2,s->storage);
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(col(th.text_muted),"Use an existing raw or QCOW2 disk file from xemu, or use a save folder below. Clear your current choice to switch.");
    ImGui::TextColored(col(th.text_muted),"An existing disk needs your original kernel dump and matching EEPROM. Close xemu or other games before sharing it.");
    ImGui::Dummy(ImVec2(0,px(8)));
    ImGui::BeginDisabled(s->hdd[0]!=0);
    xboxllr_console_path(m,"Save folder","Select a save folder",s->storage,1,s->hdd);
    ImGui::EndDisabled();
    ImGui::TextColored(col(th.text),"Used only when no HDD image is selected. Leave blank: private save storage for %s is created automatically. Other games will not use it.",m->game_name?m->game_name:"this game");
    ImGui::TextColored(col(th.text_muted),"No HDD image is created automatically. An invalid or busy selected image reports a problem; it never silently switches to this folder.");
    ImGui::PopTextWrapPos();ImGui::Dummy(ImVec2(0,px(8)));
    if(ImGui::Button("Save")) {
        s->acknowledged=1;s->advanced_open=0;xboxllr_setup_persist();
    }
    ImGui::SameLine();
    if(ImGui::Button("Use defaults")) {
        s->kernel[0]=s->eeprom[0]=s->storage[0]=s->hdd[0]=0;
        s->acknowledged=1;s->advanced_open=0;xboxllr_setup_persist();
    }
    if(s->locked)ImGui::EndDisabled();
}
static void draw_xboxllr_console_welcome(LauncherModel *m,const LauncherTheme &th) {
    auto *s=xboxllr_console_setup;
    if(!s || s->acknowledged || s->advanced_open)return;
    ImGui::OpenPopup("Welcome - optional Xbox files");
    ImGui::SetNextWindowSize(ImVec2(px(620),0),ImGuiCond_Always);
    if(ImGui::BeginPopupModal("Welcome - optional Xbox files",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+px(565));
        ImGui::TextColored(col(th.accent),"Have backups from your Xbox?");
        ImGui::Spacing();
        ImGui::TextWrapped("You can add them now, or skip this. For new single-player saves, we can create a local console and save storage for you.");
        ImGui::Spacing();
        ImGui::TextWrapped("If you skip: saves from an original Xbox may not work, and new local saves are not guaranteed to work on a real Xbox.");
        ImGui::Spacing();
        ImGui::TextWrapped("Xbox Live is not supported in this build, with or without these files.");
        ImGui::Spacing();
        ImGui::TextColored(col(th.text_muted),"These options stay under Settings. Choose your game on the main screen.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0,px(12)));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-px(292));
        if(ImGui::Button("Choose console files",ImVec2(px(190),px(38)))) {
            s->advanced_open=1;m->view=LNG_VIEW_SETTINGS;ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0,px(12));
        if(ImGui::Button("Skip",ImVec2(px(90),px(38)))) {
            s->acknowledged=1;xboxllr_setup_persist();ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

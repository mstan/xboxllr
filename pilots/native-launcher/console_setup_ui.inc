/* Included by the pinned frontend adapter after its common file picker.
 * Uses the same ImGui theme, navigation and file browser as recomp-ui. */
static void xboxllr_setup_persist() {
    auto *s=xboxllr_console_setup;
    if(s && s->persist)s->persist(s->context);
}
static void xboxllr_pick_console_file(LauncherModel *m,const char *title,char *target,bool folder=false) {
    PickRequest req;
    req.title=title;req.start_path=target;
    req.mode=folder?PickMode::Folder:PickMode::OpenFile;
    if(!folder){req.patterns={"*.bin"};req.description="Xbox backup (.bin)";}
    req.on_pick=[target](const char *path){if(path)std::snprintf(target,4096,"%s",path);};
    ui_pick(m,std::move(req));
}
static void xboxllr_console_path(LauncherModel *m,const char *label,const char *picker,char *target,bool folder=false) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-px(92));
    ImGui::InputText("##path",target,4096);
    ImGui::SameLine();
    if(ImGui::Button("Browse",ImVec2(px(82),0)))xboxllr_pick_console_file(m,picker,target,folder);
    ImGui::PopID();
}
static void draw_xboxllr_console_controls(LauncherModel *m,const LauncherTheme &th) {
    auto *s=xboxllr_console_setup;if(!s)return;
    eyebrow("CONSOLE SETUP (OPTIONAL)");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(col(th.text_muted),
        "Leave these empty for a new local console and automatic save storage. "
        "You only need original Xbox files when you want to use a matching original console identity.");
    if(s->locked)ImGui::TextColored(col(th.text_muted),
        "This setup already has a save identity. It is kept unchanged to protect your progress. "
        "Use a separate fresh setup for a different console or storage.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0,px(8)));
    if(s->locked)ImGui::BeginDisabled();
    xboxllr_console_path(m,"Kernel backup","Select your original Xbox kernel backup",s->kernel);
    ImGui::TextWrapped("Only for original-compatible save signing; it is never booted during play.");
    xboxllr_console_path(m,"Console identity (EEPROM)","Select your original Xbox EEPROM backup",s->eeprom);
    ImGui::TextWrapped("Use the matching identity for console-bound Xbox saves. Requires the signing source above.");
    xboxllr_console_path(m,"Shared save-storage folder","Select an empty or existing xboxllr shared storage folder",s->storage,true);
    ImGui::TextWrapped("Empty means automatic storage in this app. Share a virtual HDD folder between titles to use one console and its saves. Only one game can write it at a time.");
    ImGui::TextColored(col(th.text_muted),"Original HDD image import is not available in this preview.");
    if(ImGui::Button("Save console setup")) {
        s->acknowledged=1;s->advanced_open=0;xboxllr_setup_persist();
    }
    ImGui::SameLine();
    if(ImGui::Button("Use defaults")) {
        s->kernel[0]=s->eeprom[0]=s->storage[0]=0;
        s->acknowledged=1;s->advanced_open=0;xboxllr_setup_persist();
    }
    if(s->locked)ImGui::EndDisabled();
    if(s->acknowledged)ImGui::TextColored(col(th.good),"Setup choices saved.");
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
        if(ImGui::Button("Skip and continue",ImVec2(px(200),px(38)))) {
            s->acknowledged=1;xboxllr_setup_persist();ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Choose console files",ImVec2(px(200),px(38)))) {
            s->advanced_open=1;m->view=LNG_VIEW_SETTINGS;ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

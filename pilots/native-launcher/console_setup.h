#pragma once

/* Host-owned first-use state for the shared prepared recomp-ui frontend.
 * No game addresses, emulation services or secret bytes belong in the UI. */
struct XboxllrGraffitiDesign {
    int slot=0,size=0,density=1,width=0,height=0,thumb_width=0,thumb_height=0;
    bool deletable=false;
    char sha[65]={};
    const unsigned char *pixels=nullptr;
};
struct XboxllrConsoleSetup {
    int acknowledged=0, locked=0, advanced_open=0;
    char kernel[4096]={}, eeprom[4096]={}, storage[4096]={}, hdd[4096]={};
    bool boot_available=false, boot_enabled=false;
    char boot_kernel[4096]={}, boot_status[512]={};
    void (*persist_boot)(void *)=nullptr;
    void (*persist)(void *)=nullptr;
    bool image_import=false,import_pending=false,refresh_capacity=true;
    int free_slots=-1;
    char import_status[1024]={},import_capacity[512]={};
    int (*submit_image)(void*,const char*,const char*,int,int,int,const unsigned char*,int,int)=nullptr;
    int (*manage_image)(void*,const char*,const char*,int,const char*)=nullptr;
    int (*saved_image)(void*,int,XboxllrGraffitiDesign*)=nullptr;
    const unsigned char *(*preview_image)(void*,int*,int*)=nullptr;
    unsigned gallery_version=0,preview_version=0;
    int saved_count=0,preview_slot=-1;
    bool gallery_ready=false;
    void (*poll_image)(void*)=nullptr;
    void *context=nullptr;
};
extern XboxllrConsoleSetup *xboxllr_console_setup;

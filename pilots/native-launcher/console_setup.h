#pragma once

/* Host-owned first-use state for the shared prepared recomp-ui frontend.
 * No game addresses, emulation services or secret bytes belong in the UI. */
struct XboxllrConsoleSetup {
    int acknowledged=0, locked=0, advanced_open=0;
    char kernel[4096]={}, eeprom[4096]={}, storage[4096]={}, hdd[4096]={};
    bool boot_available=false, boot_enabled=false;
    char boot_kernel[4096]={}, boot_status[512]={};
    void (*persist_boot)(void *)=nullptr;
    void (*persist)(void *)=nullptr;
    void *context=nullptr;
};
extern XboxllrConsoleSetup *xboxllr_console_setup;

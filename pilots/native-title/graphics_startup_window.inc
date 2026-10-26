/* Shared native presentation: the title owns its HWND before, during and after
 * startup. Only the packaged sibling module is admitted; no foreign windows,
 * global lookup, reparenting, host geometry changes or guest state transfer. */
#include "../native-boot/boot_window_api.h"
static int pilot_startup_window_attempted;
static void pilot_startup_window(void)
{
    if(pilot_startup_window_attempted)return;
    pilot_startup_window_attempted=1;
    WCHAR module[4096],pack[4096],logo[4096],report[4096],expected[4096];
    DWORD length=GetEnvironmentVariableW(L"XLLE_PILOT_BOOT_MODULE",module,4096);
    if(!length)return;
    if(length>=4096)goto failed;
    length=GetModuleFileNameW(NULL,expected,4096);
    if(!length || length>=4096)goto failed;
    WCHAR *slash=wcsrchr(expected,L'\\');
    if(!slash || (size_t)(slash-expected)+17>=4096)goto failed;
    wcscpy(slash+1,L"XboxStartup.dll");
    if(_wcsicmp(module,expected))goto failed;
    length=GetEnvironmentVariableW(L"XLLE_PILOT_BOOT_PACK",pack,4096);
    if(!length || length>=4096)goto failed;
    length=GetEnvironmentVariableW(L"XLLE_PILOT_BOOT_LOGO",logo,4096);
    if(!length || length>=4096)goto failed;
    length=GetEnvironmentVariableW(L"XLLE_PILOT_BOOT_REPORT",report,4096);
    if(!length || length>=4096)goto failed;
    HMODULE library=LoadLibraryExW(module,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!library)goto failed;
    XboxllrBootRunWindowV1 run=(XboxllrBootRunWindowV1)(void*)GetProcAddress(library,"XboxllrStartupRunWindowV1");
    XboxllrBootWindowV1 options={0};options.size=sizeof(options);
    options.window=pilot_window;options.pack=pack;options.title_logo=logo;options.report=report;
    const char *volume=getenv("RECOMP_VOLUME"),*visible=getenv("XLLE_PILOT_VISIBLE"),
               *mute=getenv("XLLE_PILOT_AUDIO_OUTPUT_MUTE");
    char *end=NULL;unsigned long number=volume?strtoul(volume,&end,10):100;
    options.volume=volume && (!*volume || *end || number>100)?100:(uint32_t)number;
    if(mute && !strcmp(mute,"1"))options.flags|=XBOXLLR_BOOT_WINDOW_MUTE;
    if(!visible || strcmp(visible,"1"))options.flags|=XBOXLLR_BOOT_WINDOW_HIDDEN;
    int result=run?run(&options):XBOXLLR_BOOT_WINDOW_FAILED;
    /* DXGI may retain/restore a swap-chain window thunk after GPU teardown.
     * Keep the authored code loaded for the application's window lifetime;
     * the inactive thunk forwards all input/messages to the title procedure. */
    if(result==XBOXLLR_BOOT_WINDOW_CLOSED)ExitProcess(0); /* Owner closed this app. */
    pilot_visible=IsWindowVisible(pilot_window)!=0;
    if(pilot_visible)pilot_window_host_show(&pilot_window_host);
    fprintf(stderr,"[STARTUP] Same title window %p returned %d\n",(void*)pilot_window,result);
    fflush(stderr);
    return;
failed:
    fprintf(stderr,"[STARTUP] Packaged startup module unavailable; continuing in the same title window.\n");
    fflush(stderr);
}

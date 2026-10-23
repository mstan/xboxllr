/* Exercise SDL2's real Windows notification pump without SDL video or a
 * physical controller. Virtual joystick samples do not exercise WM_INPUT. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "input_sdl.h"

static HWND notification_window(void)
{
    HWND window=NULL;
    while((window=FindWindowExW(HWND_MESSAGE,window,L"Message",NULL))!=NULL) {
        DWORD process=0;
        GetWindowThreadProcessId(window,&process);
        if(process==GetCurrentProcessId())return window;
    }
    return NULL;
}

int main(void)
{
    /* No SDL initialization on this thread: production owns initialization
     * and must service its message-only window while the caller is idle. */
    uint32_t status=pilot_input_sdl_start("00000000000000000000000000000000");
    if(status){fprintf(stderr,"FAIL transport start: %u\n",status);return 1;}
    HWND window=NULL;
    for(unsigned i=0;i<250&&!window;++i){window=notification_window();if(!window)Sleep(4);}
    int result=0;
    if(!window){fprintf(stderr,"FAIL SDL notification window missing\n");result=1;}
    else {
        DWORD_PTR reply=0;
        /* WM_NULL has no input or UI effect; target only this process's SDL
         * helper. An enumerated but unpumped RawInput window times out. */
        if(!SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&reply)) {
            fprintf(stderr,"FAIL SDL notification window is not dispatching messages (%lu)\n",GetLastError());
            result=1;
        }
    }
    pilot_input_sdl_shutdown();
    if(notification_window()){fprintf(stderr,"FAIL SDL notification window leaked\n");result=1;}
    if(!result)puts("PASS headless SDL Windows message dispatch and shutdown");
    return result;
}

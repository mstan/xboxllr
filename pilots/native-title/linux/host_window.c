/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "windows.h"
#include <SDL.h>
static SDL_Window *keyboard_focus;
static DWORD keyboard_thread;
static unsigned char keyboard_state[SDL_NUM_SCANCODES];
void pilot_posix_keyboard_snapshot(void)
{
    const Uint8 *state=SDL_GetKeyboardState(NULL);
    for(unsigned i=0;i<SDL_NUM_SCANCODES;++i)__atomic_store_n(keyboard_state+i,state[i],__ATOMIC_RELAXED);
    __atomic_store_n(&keyboard_thread,GetCurrentThreadId(),__ATOMIC_RELAXED);
    __atomic_store_n(&keyboard_focus,SDL_GetKeyboardFocus(),__ATOMIC_RELEASE);
}
BOOL GetClientRect(HWND window,RECT *out)
{if(!window || !out)return FALSE;int w,h;SDL_GetWindowSize(window,&w,&h);*out=(RECT){0,0,w,h};return TRUE;}
HWND GetForegroundWindow(void){return __atomic_load_n(&keyboard_focus,__ATOMIC_ACQUIRE);}
DWORD GetWindowThreadProcessId(HWND window,DWORD *process)
{if(process)*process=window?GetCurrentProcessId():0;return window?__atomic_load_n(&keyboard_thread,__ATOMIC_RELAXED):0;}
SHORT GetAsyncKeyState(int key)
{
    if(!GetForegroundWindow())return 0;
    SDL_Scancode scan=SDL_SCANCODE_UNKNOWN;
    if(key>='A' && key<='Z')scan=(SDL_Scancode)(SDL_SCANCODE_A+key-'A');
    else if(key>='1' && key<='9')scan=(SDL_Scancode)(SDL_SCANCODE_1+key-'1');
    else switch(key){case VK_UP:scan=SDL_SCANCODE_UP;break;case VK_DOWN:scan=SDL_SCANCODE_DOWN;break;case VK_LEFT:scan=SDL_SCANCODE_LEFT;break;case VK_RIGHT:scan=SDL_SCANCODE_RIGHT;break;case VK_RETURN:scan=SDL_SCANCODE_RETURN;break;case VK_BACK:scan=SDL_SCANCODE_BACKSPACE;break;case VK_SPACE:scan=SDL_SCANCODE_SPACE;break;case VK_SHIFT:scan=SDL_SCANCODE_LSHIFT;break;default:break;}
    return scan!=SDL_SCANCODE_UNKNOWN && __atomic_load_n(keyboard_state+scan,__ATOMIC_RELAXED)?(SHORT)0x8000:0;
}

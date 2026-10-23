/* SPDX-License-Identifier: GPL-3.0-or-later
 * Windowed portable-package entry point. No shell or developer console. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR arguments,int show)
{
    (void)instance;(void)previous;(void)show;
    WCHAR root[4096],program[4096];
    DWORD length=GetModuleFileNameW(NULL,root,4096);
    if(!length || length>=4096)goto failed;
    WCHAR *slash=wcsrchr(root,L'\\');
    if(!slash)goto failed;
    *slash=0;
    int written=swprintf(program,4096,L"%ls\\python\\pythonw.exe",root);
    if(written<0 || written>=4096)goto failed;
    size_t capacity=wcslen(root)*2+wcslen(arguments?arguments:L"")+128;
    if(capacity>32768)goto failed;
    WCHAR *command=HeapAlloc(GetProcessHeap(),0,capacity*sizeof(WCHAR));
    if(!command)goto failed;
    written=swprintf(command,capacity,L"\"%ls\" \"%ls\\tools\\native_release_runtime.py\" %ls",
                     program,root,arguments?arguments:L"");
    if(written<0 || (size_t)written>=capacity){HeapFree(GetProcessHeap(),0,command);goto failed;}
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION child={0};
    BOOL created=CreateProcessW(program,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,root,&startup,&child);
    HeapFree(GetProcessHeap(),0,command);
    if(!created)goto failed;
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
    return 0;
failed:
    MessageBoxW(NULL,L"The launcher could not start. Extract the complete download into a writable folder, then open Start Game.exe again.",
                L"Launcher",MB_OK|MB_ICONERROR);
    return 2;
}

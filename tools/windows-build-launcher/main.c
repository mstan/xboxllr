/* SPDX-License-Identifier: GPL-3.0-or-later
 * Console entry point for the bundled Python build drivers. No shell is used.
 * Preserve the caller's working directory and exact original argument tail. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int wmain(void)
{
    WCHAR root[32768], python[32768];
    DWORD size=GetModuleFileNameW(NULL,root,32768);
    if(!size || size>=32768)goto failed;
    WCHAR *slash=wcsrchr(root,L'\\');
    if(!slash)goto failed;
    *slash=0;
    int count=swprintf(python,32768,L"%ls\\python\\python.exe",root);
    if(count<0 || count>=32768)goto failed;
    const WCHAR *tail=GetCommandLineW();
    /* The first token is Windows' executable path; quotes cannot occur in it. */
    if(*tail==L'"') { ++tail; while(*tail && *tail!=L'"')++tail; if(*tail)++tail; }
    else { while(*tail && *tail!=L' ' && *tail!=L'\t')++tail; }
    while(*tail==L' ' || *tail==L'\t')++tail;
    size_t capacity=wcslen(python)+wcslen(root)+wcslen(tail)+80;
    if(capacity>32767)goto failed;
    WCHAR *line=HeapAlloc(GetProcessHeap(),0,capacity*sizeof(WCHAR));
    if(!line)goto failed;
    count=swprintf(line,capacity,L"\"%ls\" -B \"%ls\\tools\\xboxllr.py\" %ls",python,root,tail);
    if(count<0 || (size_t)count>=capacity){HeapFree(GetProcessHeap(),0,line);goto failed;}
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);
    PROCESS_INFORMATION child={0};
    BOOL created=CreateProcessW(python,line,NULL,NULL,TRUE,0,NULL,NULL,&startup,&child);
    HeapFree(GetProcessHeap(),0,line);
    if(!created)goto failed;
    CloseHandle(child.hThread);
    WaitForSingleObject(child.hProcess,INFINITE);
    DWORD status=2;
    GetExitCodeProcess(child.hProcess,&status);
    CloseHandle(child.hProcess);
    return (int)status;
failed:
    fwprintf(stderr,L"XboxLLR could not start (Windows error %lu). Extract the complete toolkit ZIP, including python/ and tools/.\n",GetLastError());
    return 2;
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native HWND presentation policy. No display mode changes or guest state.
 * WINDOWPLACEMENT coordinates are used only with Get/SetWindowPlacement.
 * The launcher supplies a per-title settings path; unattended QA omits it. */
#ifndef PILOT_WINDOW_HOST_H
#define PILOT_WINDOW_HOST_H
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
struct PilotWindowHost {
    HWND window;
    WINDOWPLACEMENT placement;
    WCHAR path[4096];
    LONG_PTR style;
    int borderless,maximized,changing,shown;
};
static void pilot_window_host_capture(struct PilotWindowHost *host)
{
    if(host->borderless || IsIconic(host->window))return;
    host->placement.length=sizeof(host->placement);
    if(GetWindowPlacement(host->window,&host->placement)) {
        if(host->shown)host->maximized=IsZoomed(host->window)!=0;
        host->placement.showCmd=host->maximized?SW_SHOWMAXIMIZED:SW_SHOWNORMAL;
    }
}
static void pilot_window_host_save(struct PilotWindowHost *host)
{
    if(!host->path[0] || host->changing)return;
    pilot_window_host_capture(host);
    WCHAR temporary[4160];
    if(swprintf(temporary,4160,L"%ls.%lu.tmp",host->path,GetCurrentProcessId())<0)return;
    const RECT *r=&host->placement.rcNormalPosition;
    char text[256];int count=snprintf(text,sizeof(text),"xboxllr-window 1\n%ld %ld %ld %ld %d %d\n",
        r->left,r->top,r->right,r->bottom,host->maximized,host->borderless);
    if(count<0 || count>=(int)sizeof(text))return;
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;BOOL ok=WriteFile(file,text,(DWORD)count,&written,NULL);
    if(ok)ok=written==(DWORD)count && FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok || !MoveFileExW(temporary,host->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary);
        fprintf(stderr,"[WINDOW] Could not save presentation settings (%lu)\n",GetLastError());
    }
}
static int pilot_window_host_borderless(struct PilotWindowHost *host,int enabled)
{
    if(host->borderless==enabled)return 1;
    host->changing=1;
    if(enabled) {
        pilot_window_host_capture(host);
        MONITORINFO monitor={0};monitor.cbSize=sizeof(monitor);
        if(!GetMonitorInfoW(MonitorFromWindow(host->window,MONITOR_DEFAULTTONEAREST),&monitor))goto failed;
        /* Clear the maximized bit while borderless; retain it in placement. */
        SetWindowLongPtrW(host->window,GWL_STYLE,
            (host->style&~(WS_OVERLAPPEDWINDOW|WS_MAXIMIZE))|WS_POPUP|
            (GetWindowLongPtrW(host->window,GWL_STYLE)&WS_VISIBLE));
        host->borderless=1;
        RECT r=monitor.rcMonitor;
        if(!SetWindowPos(host->window,NULL,r.left,r.top,r.right-r.left,r.bottom-r.top,
            SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))goto failed;
    } else {
        host->borderless=0;
        SetWindowLongPtrW(host->window,GWL_STYLE,host->style|
            (GetWindowLongPtrW(host->window,GWL_STYLE)&WS_VISIBLE));
        WINDOWPLACEMENT placement=host->placement;
        placement.length=sizeof(placement);
        placement.showCmd=host->shown?(host->maximized?SW_SHOWMAXIMIZED:SW_SHOWNOACTIVATE):SW_HIDE;
        if(!SetWindowPlacement(host->window,&placement) ||
           !SetWindowPos(host->window,NULL,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))goto failed;
    }
    host->changing=0;pilot_window_host_save(host);return 1;
failed:
    host->changing=0;return 0;
}
static void pilot_window_host_init(struct PilotWindowHost *host,HWND window)
{
    memset(host,0,sizeof(*host));host->window=window;
    host->style=GetWindowLongPtrW(window,GWL_STYLE);
    host->placement.length=sizeof(host->placement);
    GetWindowPlacement(window,&host->placement);
    DWORD count=GetEnvironmentVariableW(L"XLLE_PILOT_WINDOW_STATE",host->path,4096);
    if(!count || count>=4096){host->path[0]=0;return;}
    FILE *file=_wfopen(host->path,L"rb");if(!file)return;
    char header[64],tail;long l,t,r,b;int maximized,borderless;
    int valid=fgets(header,sizeof(header),file) && !strcmp(header,"xboxllr-window 1\n") &&
        fscanf(file,"%ld %ld %ld %ld %d %d",&l,&t,&r,&b,&maximized,&borderless)==6 &&
        fscanf(file," %c",&tail)==EOF;
    fclose(file);
    if(!valid || l<-1000000 || l>1000000 || t<-1000000 || t>1000000 ||
       r<-1000000 || r>1000000 || b<-1000000 || b>1000000 ||
       r-l<160 || r-l>32768 || b-t<120 || b-t>32768 ||
       (maximized!=0 && maximized!=1) || (borderless!=0 && borderless!=1))return;
    host->placement.rcNormalPosition=(RECT){l,t,r,b};
    host->maximized=maximized;
    /* Windows relocates an off-screen WINDOWPLACEMENT to an attached monitor.
     * Restore hidden, then show only when the first real frame is ready. */
    WINDOWPLACEMENT placement=host->placement;placement.showCmd=SW_HIDE;
    host->changing=1;SetWindowPlacement(window,&placement);host->changing=0;
    if(borderless)pilot_window_host_borderless(host,1);
}
static void pilot_window_host_show(struct PilotWindowHost *host)
{
    host->changing=1;host->shown=1;
    ShowWindow(host->window,host->borderless?SW_SHOWNOACTIVATE:
        host->maximized?SW_SHOWMAXIMIZED:SW_SHOWNOACTIVATE);
    UpdateWindow(host->window);host->changing=0;
}
static int pilot_window_host_message(struct PilotWindowHost *host,UINT message,WPARAM wparam,LPARAM lparam,LRESULT *result)
{
    if(!host->window)return 0;
    if(message==WM_SETCURSOR && LOWORD(lparam)==HTCLIENT) {
        SetCursor(LoadCursor(NULL,IDC_ARROW));*result=TRUE;return 1;
    }
    if(message==WM_SYSKEYDOWN && wparam==VK_RETURN && (lparam&((LPARAM)1<<29))) {
        if(!(lparam&((LPARAM)1<<30)))pilot_window_host_borderless(host,!host->borderless);
        *result=0;return 1;
    }
    if(message==WM_SYSCHAR && wparam==VK_RETURN){*result=0;return 1;}
    if(message==WM_EXITSIZEMOVE || message==WM_CLOSE)pilot_window_host_save(host);
    if(message==WM_SIZE && !host->changing && !host->borderless && wparam!=SIZE_MINIMIZED) {
        int maximized=wparam==SIZE_MAXIMIZED;
        if(host->shown && maximized!=host->maximized) {
            host->maximized=maximized;pilot_window_host_save(host);
        }
    }
    if(message==WM_DISPLAYCHANGE && host->borderless) {
        MONITORINFO monitor={0};monitor.cbSize=sizeof(monitor);
        if(GetMonitorInfoW(MonitorFromWindow(host->window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            RECT r=monitor.rcMonitor;
            SetWindowPos(host->window,NULL,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
        }
    }
    return 0;
}
#endif

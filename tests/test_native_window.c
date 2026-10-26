/* Actual hidden HWND, native messages and restart persistence; no focus change. */
#include "window_host.h"
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %u: %s error=%lu\n",__LINE__,#x,GetLastError());return 1;}}while(0)
static struct PilotWindowHost host;
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l)
{
    LRESULT result;
    if(pilot_window_host_message(&host,message,w,l,&result))return result;
    return DefWindowProcW(window,message,w,l);
}
static HWND make_window(void)
{
    WNDCLASSW klass={0};klass.lpfnWndProc=procedure;klass.hInstance=GetModuleHandleW(NULL);
    klass.lpszClassName=L"xboxllr-window-policy-test";klass.hCursor=LoadCursor(NULL,IDC_ARROW);
    RegisterClassW(&klass);
    HWND window=CreateWindowW(klass.lpszClassName,L"Hidden window policy test",WS_OVERLAPPEDWINDOW,
        110,120,700,500,NULL,NULL,klass.hInstance,NULL);
    if(window)pilot_window_host_init(&host,window);
    return window;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);WCHAR path[4096];CHECK(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path,4096));
    CHECK(SetEnvironmentVariableW(L"XLLE_PILOT_WINDOW_STATE",path));
    CHECK(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    HWND window=make_window();CHECK(window && !IsWindowVisible(window));
    WINDOWPLACEMENT before={0};before.length=sizeof(before);CHECK(GetWindowPlacement(window,&before));
    SendMessageW(window,WM_EXITSIZEMOVE,0,0);CHECK(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES);
    DEVMODEW desktop={0};desktop.dmSize=sizeof(desktop);CHECK(EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,&desktop));
    SendMessageW(window,WM_SYSKEYDOWN,VK_RETURN,(LPARAM)1<<29);CHECK(host.borderless);
    CHECK(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION));CHECK(!IsWindowVisible(window));
    RECT full;CHECK(GetWindowRect(window,&full));MONITORINFO monitor={0};monitor.cbSize=sizeof(monitor);
    CHECK(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor));
    CHECK(EqualRect(&full,&monitor.rcMonitor));
    SendMessageW(window,WM_SYSKEYDOWN,VK_RETURN,((LPARAM)1<<29)|((LPARAM)1<<30));CHECK(host.borderless);
    CHECK(SendMessageW(window,WM_SETCURSOR,(WPARAM)window,HTCLIENT)==TRUE);
    CHECK(GetCursor()==LoadCursor(NULL,IDC_ARROW));
    DestroyWindow(window);memset(&host,0,sizeof(host));window=make_window();CHECK(window && host.borderless);
    SendMessageW(window,WM_SYSKEYDOWN,VK_RETURN,(LPARAM)1<<29);CHECK(!host.borderless);
    WINDOWPLACEMENT after={0};after.length=sizeof(after);CHECK(GetWindowPlacement(window,&after));
    CHECK(EqualRect(&before.rcNormalPosition,&after.rcNormalPosition));
    CHECK(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION);
    DEVMODEW current={0};current.dmSize=sizeof(current);CHECK(EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,&current));
    CHECK(desktop.dmPelsWidth==current.dmPelsWidth && desktop.dmPelsHeight==current.dmPelsHeight &&
        desktop.dmDisplayFrequency==current.dmDisplayFrequency);
    DestroyWindow(window);memset(&host,0,sizeof(host));
    FILE *file=_wfopen(path,L"wb");CHECK(file);fputs("xboxllr-window 1\n110 120 810 620 1 0\n",file);CHECK(!fclose(file));
    window=make_window();CHECK(window && host.maximized && !host.borderless && !IsWindowVisible(window));
    /* Exercise show policy without drawing or activating the window. */
    if(0)pilot_window_host_show(&host);
    DestroyWindow(window);memset(&host,0,sizeof(host));
    file=_wfopen(path,L"wb");CHECK(file);fputs("xboxllr-window 1\n0 0 999999999 4 1 1\n",file);CHECK(!fclose(file));
    window=make_window();CHECK(window && !host.maximized && !host.borderless && !IsWindowVisible(window));
    DestroyWindow(window);CHECK(DeleteFileW(path));
    puts("PASS borderless Alt+Enter/repeat, unchanged desktop mode, arrow cursor, restart geometry/fullscreen/maximize and invalid settings");return 0;
}

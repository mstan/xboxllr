/* Exercise the production observation reader with real Windows handles only;
 * no title, GPU, audio, original firmware or writable guest image is needed. */
#include "gpu_framebuffer_request.h"
static unsigned failures;
#define CHECK(condition) do {if(!(condition)) {fprintf(stderr,"FAIL line%u: %s (error%lu)\n",__LINE__,#condition,(unsigned long)GetLastError());++failures;}} while(0)
static void marker(const char *path,const char *bytes,DWORD size)
{
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE);if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;CHECK(WriteFile(file,bytes,size,&written,NULL) && written==size);
    CHECK(CloseHandle(file));
}
int main(void)
{
    char temporary[MAX_PATH],directory[MAX_PATH],path[MAX_PATH],staged[MAX_PATH];
    CHECK(GetTempPathA(sizeof(temporary),temporary)>0);
    CHECK(GetTempFileNameA(temporary,"gfr",0,directory) && DeleteFileA(directory));
    CHECK(CreateDirectoryA(directory,NULL));
    CHECK(sprintf_s(path,sizeof(path),"%s/framebuffer-01.request",directory)>0);
    CHECK(sprintf_s(staged,sizeof(staged),"%s/staged",directory)>0);
    CHECK(gpu_framebuffer_request(directory,1)==0);
    CHECK(gpu_framebuffer_request(directory,0)==-1 && gpu_framebuffer_request(directory,17)==-1);
    marker(staged,"capture-v1\n",11);
    HANDLE publisher=CreateFileA(staged,DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(publisher!=INVALID_HANDLE_VALUE && MoveFileExA(staged,path,0));
    HANDLE old_reader=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(old_reader==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION);
    if(old_reader!=INVALID_HANDLE_VALUE)CHECK(CloseHandle(old_reader));
    CHECK(gpu_framebuffer_request(directory,1)==1);
    CHECK(CloseHandle(publisher));
    CHECK(gpu_framebuffer_request(directory,1)==1 && gpu_framebuffer_request(directory,2)==0);
    HANDLE writer=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                             NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(writer!=INVALID_HANDLE_VALUE && gpu_framebuffer_request(directory,1)==-1);
    CHECK(CloseHandle(writer));CHECK(DeleteFileA(path));
    const char *invalid[]={"capture-v1","capture-v1\nx","capture-v2\n","capture-v1\r\n"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        marker(path,invalid[i],(DWORD)strlen(invalid[i]));
        CHECK(gpu_framebuffer_request(directory,1)==-1);CHECK(DeleteFileA(path));
    }
    CHECK(CreateDirectoryA(path,NULL) && gpu_framebuffer_request(directory,1)==-1);
    CHECK(RemoveDirectoryA(path) && RemoveDirectoryA(directory));
    CHECK(gpu_framebuffer_request(directory,1)==-1);
    if(!failures)puts("PASS framebuffer request: atomic rename handle, content-writer exclusion, exact bytes, absent and invalid paths");
    return failures?1:0;
}

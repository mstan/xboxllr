/* Deterministic control transport exercises the real input_host capture/replay
 * boundary without polling physical hardware, a title or the renderer. */
#include "input_host.c"
int pilot_debug_active(void){return 0;}
int pilot_debug_input(uint8_t pad[18]){(void)pad;return 0;}
int pilot_debug_input_port(unsigned port,uint8_t pad[18]){(void)port;(void)pad;return 0;}
int pilot_debug_connected(unsigned port){(void)port;return 0;}
uint32_t pilot_input_sdl_start(const char *guid){(void)guid;return ERROR_NOT_SUPPORTED;}
uint32_t pilot_input_sdl_state(unsigned port,uint8_t pad[18]){(void)port;(void)pad;return ERROR_NOT_SUPPORTED;}
uint32_t pilot_input_sdl_caps(unsigned port,uint8_t pad[18],uint8_t rumble[4])
{(void)port;(void)pad;(void)rumble;return ERROR_NOT_SUPPORTED;}
uint32_t pilot_input_sdl_rumble(unsigned port,uint16_t left,uint16_t right)
{(void)port;(void)left;(void)right;return ERROR_NOT_SUPPORTED;}
#define CHECK(v) do {if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);return 1;}}while(0)
int main(int argc,char **argv)
{
    CHECK(argc==3 && (argv[1][0]=='0'||argv[1][0]=='1') && (argv[2][0]=='0'||argv[2][0]=='1'));
    char name[96];snprintf(name,sizeof(name),"Local\\xlle-input-camera-%lu",(unsigned long)GetCurrentProcessId());
    HANDLE mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,64,name);CHECK(mapping!=NULL);
    struct InputControlReport *report=MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,64);CHECK(report!=NULL);
    memset(report,0,64);memcpy(report->magic,"XLLEPAD1",8);
    report->sequence=2;report->lease_ms=5000;report->issued_ms=GetTickCount64();
    const uint8_t original[18]={0x10,0,0xff,0,0xff,0,0,0,20,30,0,0x80,0xff,0x7f,0,0x80,0x39,0x30};
    memcpy(report->pad,original,18);
    CHECK(!_putenv_s("XLLE_PILOT_INPUT","control"));CHECK(!_putenv_s("XLLE_PILOT_INPUT_CONTROL",name));
    CHECK(!_putenv_s("XLLE_PILOT_CAMERA_INVERT_X",argv[1]));CHECK(!_putenv_s("XLLE_PILOT_CAMERA_INVERT_Y",argv[2]));
    uint8_t pad[18],caps[18],rumble[4];CHECK(!pilot_input_host_state(0,pad));
    CHECK(!memcmp(original,pad,14));
    CHECK(pad[14]==(argv[1][0]=='1'?0xff:0) && pad[15]==(argv[1][0]=='1'?0x7f:0x80));
    CHECK(pad[16]==(argv[2][0]=='1'?0xc7:0x39) && pad[17]==(argv[2][0]=='1'?0xcf:0x30));
    CHECK(!pilot_input_host_caps(0,caps,rumble));
    CHECK(caps[14]==255&&caps[15]==255&&caps[16]==255&&caps[17]==255);
    memset(pad,0xcc,18);CHECK(pilot_input_host_state(1,pad)==ERROR_DEVICE_NOT_CONNECTED);
    for(unsigned i=0;i<18;++i)CHECK(pad[i]==0xcc);
    CHECK(!pilot_input_host_rumble(0,0,0));
    pilot_input_diagnostic_flush();
    UnmapViewOfFile(report);CloseHandle(mapping);
    puts("PASS actual host input boundary, capability mask, port isolation and capture/replay mapping");return 0;
}

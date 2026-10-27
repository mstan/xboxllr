/* Exercise the actual host boundary with a hot-unplugged SDL device. */
#include "input_host.c"
static int debug_enabled,leased,connected;
static unsigned debug_mask=1,leased_port;
static uint32_t device_error=ERROR_DEVICE_NOT_CONNECTED;
static uint8_t leased_pad[18],physical_pad[18];
int pilot_debug_active(void){return debug_enabled;}
int pilot_debug_connected(unsigned port){return debug_enabled && port<4 && (debug_mask&(1u<<port));}
int pilot_debug_input(uint8_t pad[18])
{if(!debug_enabled || !leased)return 0;memcpy(pad,leased_pad,18);return 1;}
int pilot_debug_input_port(unsigned port,uint8_t pad[18])
{return port==leased_port && pilot_debug_connected(port)?pilot_debug_input(pad):0;}
uint32_t pilot_input_sdl_start(const char *guid){(void)guid;return ERROR_SUCCESS;}
uint32_t pilot_input_sdl_state(unsigned port,uint8_t pad[18])
{if(port || !connected)return device_error;memcpy(pad,physical_pad,18);return ERROR_SUCCESS;}
uint32_t pilot_input_sdl_caps(unsigned port,uint8_t pad[18],uint8_t rumble[4])
{if(port || !connected)return device_error;memset(pad,0x55,18);memset(rumble,0xff,4);return ERROR_SUCCESS;}
uint32_t pilot_input_sdl_rumble(unsigned port,uint16_t left,uint16_t right)
{(void)left;(void)right;return port || !connected?device_error:ERROR_SUCCESS;}
#define CHECK(v) do {if(!(v)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);return 1;}}while(0)
int main(int argc,char **argv)
{
    CHECK(argc==2);CHECK(!_putenv_s("XLLE_PILOT_INPUT",argv[1]));
    uint8_t pad[18],caps[18],rumble[4],neutral[18]={0};
    memset(pad,0xcc,18);CHECK(pilot_input_host_state(0,pad)==ERROR_DEVICE_NOT_CONNECTED);
    CHECK(pad[0]==0xcc);debug_enabled=1;
    if(!strcmp(argv[1],"none")) {
        CHECK(pilot_input_host_state(0,pad)==ERROR_DEVICE_NOT_CONNECTED);
        CHECK(pilot_input_host_caps(0,caps,rumble)==ERROR_DEVICE_NOT_CONNECTED);
        puts("PASS explicit absent-controller fixture remains absent");return 0;
    }
    for(unsigned cycle=0;cycle<4;++cycle) {
        CHECK(!pilot_input_host_state(0,pad));CHECK(!memcmp(pad,neutral,18));
        CHECK(!pilot_input_host_caps(0,caps,rumble));
        for(unsigned i=0;i<18;++i)CHECK(caps[i]==(i==1?0:255));
        for(unsigned i=0;i<4;++i)CHECK(!rumble[i]);
        leased_pad[0]=16;leased_pad[2]=255;leased=1;
        CHECK(!pilot_input_host_state(0,pad));CHECK(!memcmp(pad,leased_pad,18));
        CHECK(!pilot_input_host_caps(0,caps,rumble));
        leased=0;CHECK(!pilot_input_host_state(0,pad));CHECK(!memcmp(pad,neutral,18));
        connected=1;physical_pad[3]=255;
        CHECK(!pilot_input_host_state(0,pad));CHECK(!memcmp(pad,physical_pad,18));
        CHECK(!pilot_input_host_caps(0,caps,rumble));CHECK(caps[0]==0x55&&rumble[0]==255);
        CHECK(!pilot_input_host_rumble(0,120,240));connected=0;
    }
    CHECK(!pilot_input_host_rumble(0,0,0));
    CHECK(pilot_input_host_rumble(0,120,240)==ERROR_NOT_SUPPORTED);
    for(unsigned port=1;port<4;++port) {
        CHECK(pilot_input_host_state(port,pad)==ERROR_DEVICE_NOT_CONNECTED);
        CHECK(pilot_input_host_caps(port,caps,rumble)==ERROR_DEVICE_NOT_CONNECTED);
        CHECK(pilot_input_host_rumble(port,0,0)==ERROR_DEVICE_NOT_CONNECTED);
    }
    debug_mask=3;
    CHECK(!pilot_input_host_state(1,pad));CHECK(!memcmp(pad,neutral,18));
    CHECK(!pilot_input_host_caps(1,caps,rumble));CHECK(!pilot_input_host_rumble(1,0,0));
    leased_port=1;leased=1;leased_pad[3]=255;
    CHECK(!pilot_input_host_state(1,pad));CHECK(!memcmp(pad,leased_pad,18));
    CHECK(!pilot_input_host_state(0,pad));CHECK(!memcmp(pad,neutral,18));
    leased=0;debug_mask=1;CHECK(pilot_input_host_state(1,pad)==ERROR_DEVICE_NOT_CONNECTED);
    device_error=ERROR_NOT_SUPPORTED;
    CHECK(pilot_input_host_state(0,pad)==ERROR_NOT_SUPPORTED);
    CHECK(pilot_input_host_caps(0,caps,rumble)==ERROR_NOT_SUPPORTED);
    debug_enabled=0;device_error=ERROR_DEVICE_NOT_CONNECTED;
    CHECK(pilot_input_host_state(0,pad)==ERROR_DEVICE_NOT_CONNECTED);
    pilot_input_diagnostic_flush();puts("PASS virtual P1 presence, hotplug, neutral releases and port isolation");return 0;
}

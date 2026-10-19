/* Authored EEPROM/artifact only; never output identity/key/region payloads. */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char memory[65536],before[65536],eeprom[256];
static int have_eeprom=1,expected_guard;
static SRWLOCK eeprom_lock=SRWLOCK_INIT;
static uint32_t g_eax,g_esp=0x8000;
static char fixture_path[MAX_PATH];
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static uint32_t *guest_word(uint32_t address)
{
    CHECK(TryAcquireSRWLockExclusive(&eeprom_lock));ReleaseSRWLockExclusive(&eeprom_lock);
    return (uint32_t *)(memory+address);
}
#define MEM32(a) (*guest_word((uint32_t)(a)))
static void stop(const char *reason,uint32_t value)
{
    (void)value;
    if(expected_guard && !memcmp(before,memory,sizeof(memory)) && g_eax==0x12345678 && g_esp==0x8000) {
        if(fixture_path[0])DeleteFileA(fixture_path);
        fprintf(stderr,"guard:%s\n",reason);ExitProcess(42);
    }
    fprintf(stderr,"unexpected/dirty guard:%s\n",reason);ExitProcess(43);
}
#include "settings_region.inc"
static void poison(void){memset(memory,0xcd,sizeof(memory));g_eax=0x12345678;}
static void expect_guard(void){poison();memcpy(before,memory,sizeof(memory));expected_guard=1;}
static void write_artifact(const unsigned char *blob,unsigned bytes)
{
    FILE *file=fopen(fixture_path,"wb");CHECK(file && fwrite(blob,1,bytes,file)==bytes && !fclose(file));
}
int main(int argc,char **argv)
{
    for(unsigned i=0;i<256;++i)eeprom[i]=(unsigned char)(i*37+11);
    unsigned char blob[45]={0};memcpy(blob,"XLLESET1",8);
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,eeprom,256,blob+8,32)>=0);blob[40]=2;
    char temporary[MAX_PATH];CHECK(GetTempPathA(sizeof(temporary),temporary));
    CHECK(GetTempFileNameA(temporary,"nvr",0,fixture_path));
    CHECK(!_putenv_s("XLLE_PILOT_CONSOLE_SETTINGS_FILE",fixture_path));
    poison();memcpy(before,memory,sizeof(memory));
    CHECK(!settings_region_query(0x103,0,0,0,0) && !memcmp(before,memory,sizeof(memory)) && g_eax==0x12345678);
    if(argc>1 && !strcmp(argv[1],"unbound")){expect_guard();settings_region_query(0x104,0x1000,0x2000,4,0);return 1;}
    if(argc>1 && !strcmp(argv[1],"no-input"))have_eeprom=0;
    settings_region_bind_boot(); /* Still no artifact opened or required. */
    if(argc>1) {
        unsigned bytes=44;
        if(!strcmp(argv[1],"magic"))blob[0]^=1;
        if(!strcmp(argv[1],"identity"))blob[8]^=1;
        if(!strcmp(argv[1],"zero"))blob[40]=0;
        if(!strcmp(argv[1],"multiple"))blob[40]=3;
        if(!strcmp(argv[1],"short"))bytes=43;
        if(!strcmp(argv[1],"trailing"))bytes=45;
        write_artifact(blob,bytes);
        if(!strcmp(argv[1],"missing"))CHECK(DeleteFileA(fixture_path));
        expect_guard();
        if(!strcmp(argv[1],"rebind"))settings_region_bind_boot();
        else if(!strcmp(argv[1],"type-null"))settings_region_query(0x104,0,0x2000,4,0x1004);
        else if(!strcmp(argv[1],"value-null"))settings_region_query(0x104,0x1000,0,4,0x1004);
        else if(!strcmp(argv[1],"type-range"))settings_region_query(0x104,0xfffffffdu,0x2000,4,0x1004);
        else if(!strcmp(argv[1],"value-range"))settings_region_query(0x104,0x1000,0xfffffffdu,4,0x1004);
        else if(!strcmp(argv[1],"result-range"))settings_region_query(0x104,0x1000,0x2000,0,0xfffffffdu);
        else settings_region_query(0x104,0x1000,0x2000,4,0x1004);
        return 1;
    }
    /* Raw EEPROM may change BEFORE the first104; identity remains boot-bound. */
    eeprom[0]^=1;write_artifact(blob,44);
    for(unsigned size=0;size<4;++size) {
        poison();CHECK(settings_region_query(0x104,0,0,size,0x1004));
        CHECK(g_eax==0xc0000023u && MEM32(0x1004)==4 && MEM32(0x1000)==0xcdcdcdcd && MEM32(0x2000)==0xcdcdcdcd);
    }
    poison();CHECK(settings_region_query(0x104,0,0,0,0) && g_eax==0xc0000023u);
    for(unsigned size=4;size<16;++size) {
        poison();CHECK(settings_region_query(0x104,0x1000,0x2000,size,0x1004) && !g_eax);
        CHECK(MEM32(0x1000)==4 && MEM32(0x1004)==4 && MEM32(0x2000)==2 && MEM32(0x2004)==0xcdcdcdcd);
        CHECK(g_esp==0x8000);
    }
    poison();CHECK(settings_region_query(0x104,0x1000,0x2000,UINT32_MAX,0) && !g_eax && MEM32(0x2000)==2);
    CHECK(MEM32(0x2004)==0xcdcdcdcd && MEM32(0x1004)==0xcdcdcdcd);
    poison();CHECK(settings_region_query(0x104,0x1000,0x2000,0,0x2000) && g_eax==0xc0000023u && MEM32(0x2000)==4);
    poison();CHECK(settings_region_query(0x104,0x1000,0x2000,8,0x2000) && !g_eax && MEM32(0x2000)==2);
    poison();CHECK(settings_region_query(0x104,0x2000,0x2000,8,0x1004) && !g_eax && MEM32(0x2000)==2);
    poison();CHECK(settings_region_query(0x104,0x1000,0x2000,8,0x1000) && !g_eax && MEM32(0x1000)==4);
    CHECK(DeleteFileA(fixture_path));eeprom[20]^=1;
    poison();CHECK(settings_region_query(0x104,0x1000,0x2000,4,0) && !g_eax && MEM32(0x2000)==2);
    const char *negative[]={"unbound","no-input","missing","magic","identity","zero","multiple","short","trailing","rebind","type-null","value-null","type-range","value-range","result-range"};
    for(unsigned i=0;i<sizeof(negative)/sizeof(negative[0]);++i) {
        char command[4096];snprintf(command,sizeof(command),"\"%s\" %s",argv[0],negative[i]);
        STARTUPINFOA si={0};PROCESS_INFORMATION pi;si.cb=sizeof(si);
        CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
        CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);DWORD code;
        CHECK(GetExitCodeProcess(pi.hProcess,&code) && code==42);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    }
    puts("PASS: boot-bound source-artifact region ABI, cache, short/alias/no-tail semantics and guarded invalid ownership");
    return 0;
}

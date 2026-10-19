/* Boot-derived console state is independent of mutable raw EEPROM/cache bytes.
 * No artifact is opened unless the admitted104 query is reached. */
#include "settings_region.h"
#ifndef PILOT_CONSOLE_SETTINGS_FILE
#define PILOT_CONSOLE_SETTINGS_FILE ""
#endif
static LONG settings_region_boot_phase;
static unsigned char settings_region_boot_identity[32];
static INIT_ONCE settings_region_once=INIT_ONCE_STATIC_INIT;
static uint32_t settings_region_value;
static void settings_region_bind_boot(void)
{
    if(InterlockedCompareExchange(&settings_region_boot_phase,1,0))
        stop("console settings boot identity is immutable",0);
    unsigned char snapshot[256];int present;
    AcquireSRWLockShared(&eeprom_lock);
    present=have_eeprom;if(present)memcpy(snapshot,eeprom,256);
    ReleaseSRWLockShared(&eeprom_lock);
    if(!present){InterlockedExchange(&settings_region_boot_phase,3);return;}
    LONG status=BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,snapshot,256,settings_region_boot_identity,32);
    SecureZeroMemory(snapshot,sizeof(snapshot));
    if(status<0)stop("console settings startup identity hash failed",0);
    InterlockedExchange(&settings_region_boot_phase,2);
}
static BOOL CALLBACK settings_region_load(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once;(void)parameter;(void)context;
    if(InterlockedCompareExchange(&settings_region_boot_phase,0,0)!=2)
        stop("console settings require a captured startup EEPROM identity",0x104);
    const char *path=getenv("XLLE_PILOT_CONSOLE_SETTINGS_FILE");
    if(!path || !*path)path=PILOT_CONSOLE_SETTINGS_FILE;
    FILE *file=fopen(path,"rb");
    if(!file)stop("missing console settings; run tools/native_console_settings.py",0x104);
    unsigned char blob[PILOT_CONSOLE_SETTINGS_BYTES];
    size_t bytes=fread(blob,1,sizeof(blob),file);int trailing=fgetc(file),error=ferror(file),closed=fclose(file);
    uint32_t region;
    if(bytes!=sizeof(blob) || trailing!=EOF || error || closed ||
        !pilot_region_parse(blob,bytes,settings_region_boot_identity,&region))
        stop("invalid console settings version/size/identity/region",0x104);
    settings_region_value=region;SecureZeroMemory(blob,sizeof(blob));
    fprintf(stderr,"[CONSOLE SETTINGS] source-bound boot snapshot; value suppressed\n");
    return TRUE;
}
static int settings_region_query(uint32_t index,uint32_t type,uint32_t value,uint32_t length,uint32_t result)
{
    if(index!=0x104)return 0;
    if(!InitOnceExecuteOnce(&settings_region_once,settings_region_load,NULL,NULL))
        stop("console settings initialization failed",index);
    /* Only outputs used by this branch are preflighted. On a valid success
     * all ranges are checked before the original first ResultLength store. */
    if((result && result>UINT32_MAX-3) || (length>=4 &&
        (!type || type>UINT32_MAX-3 || !value || value>UINT32_MAX-3)))
        stop("invalid console settings output range",index);
    if(result)MEM32(result)=4;
    if(length<4){g_eax=0xc0000023u;return 1;}
    MEM32(type)=4;MEM32(value)=settings_region_value;g_eax=0;return 1;
}

/* Nonencrypted factory reads share the existing original-shaped first-read
 * EEPROM cache with user settings. No persistence or encrypted region owner. */
#include "settings_factory.h"
static int settings_factory_query(uint32_t index,uint32_t type,uint32_t value,uint32_t length,uint32_t result)
{
    PilotFactoryField field;
    if(!pilot_factory_field(index,&field))return 0;
    if(!have_eeprom)stop("factory settings require EEPROM input",index);
    unsigned char payload[16];uint32_t status=0;
    AcquireSRWLockExclusive(&eeprom_lock);
    if(!settings_cache_valid) {
        memcpy(settings_cache,eeprom,sizeof(settings_cache));
        for(unsigned i=0;i<sizeof(settings_cache);++i)
            if(settings_cache[i]!=0xff){settings_cache_valid=1;break;}
        if(!settings_cache_valid)status=0xc0000184u;
    }
    if(!status && pilot_factory_checksum(settings_cache)!=UINT32_MAX)status=0xc000009cu;
    if(!status)memcpy(payload,settings_cache+field.offset,field.bytes);
    ReleaseSRWLockExclusive(&eeprom_lock);
    /* Original checksum failure precedes size failure; neither touches type,
     * result length or value. Do not preflight unused failure outputs. */
    if(status){g_eax=status;return 1;}
    if(length<field.bytes){g_eax=0xc0000023u;return 1;}
    if(!type || type>UINT32_MAX-3 || !value ||
        (uint64_t)value+length>UINT64_C(0x100000000) || (result && result>UINT32_MAX-3))
        stop("invalid factory settings output range",index);
    /* Keep guest memory transport outside the EEPROM owner lock. Original
     * store order matters when outputs alias: type, result, zero, payload. */
    MEM32(type)=field.type;
    if(result)MEM32(result)=field.bytes;
    memset(memory+value,0,length);memcpy(memory+value,payload,field.bytes);
    g_eax=0;return 1;
}

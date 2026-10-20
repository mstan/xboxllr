/* Independently authored XC user-setting transport. The public index layout
 * and short-write/ignored-Type behavior are cross-checked against the pinned
 * Cxbx ExSaveNonVolatileSetting reference; no reference implementation is
 * incorporated. Guest accesses precede owner locking, including aliased data. */
static uint32_t settings_user_persist(const unsigned char candidate[256]);
static int settings_user_field(uint32_t index,unsigned *offset,unsigned *size)
{
    static const unsigned char offsets[]={4,8,24,40,12,28,44,48,52,56,60,64,68,72,76,80,84,88,92};
    if(index==0xff){*offset=0x60;*size=96;return 1;}
    if(index>=sizeof(offsets))return 0;
    *offset=0x60+offsets[index];*size=4;return 1;
}
static uint32_t settings_user_checksum(const unsigned char snapshot[256])
{
    uint64_t sum=0;
    for(unsigned i=0x60;i<0xc0;i+=4){uint32_t word;memcpy(&word,snapshot+i,4);sum+=word;sum=(sum&UINT32_MAX)+(sum>>32);}
    return (uint32_t)sum;
}
static int settings_user_snapshot(void)
{
    if(!settings_cache_valid){
        memcpy(settings_cache,eeprom,256);
        for(unsigned i=0;i<256;++i)if(settings_cache[i]!=0xff){settings_cache_valid=1;break;}
    }
    return settings_cache_valid;
}
static void save_setting(void)
{
    uint32_t index=MEM32(g_esp+4),value=MEM32(g_esp+12),length=MEM32(g_esp+16);
    unsigned offset,size;
    /* Retail writes are confined to the user section; Type is unused by the
     * original public API. Factory/encrypted and unknown indices stay intact. */
    if(!settings_user_field(index,&offset,&size)){g_eax=0xc0000034u;return;}
    if(length>size){g_eax=0xc000000du;return;}
    if(length && (!value || value>UINT32_MAX-(length-1)))stop("invalid user settings input range",index);
    if(!have_eeprom)stop("user settings require EEPROM input",index);
    unsigned char payload[96]={0},candidate[256];
    if(length)memcpy(payload,memory+value,length);
    AcquireSRWLockExclusive(&eeprom_lock);
    if(!settings_user_snapshot()){ReleaseSRWLockExclusive(&eeprom_lock);g_eax=0xc0000184u;return;}
    memcpy(candidate,settings_cache,256);memset(candidate+offset,0,size);memcpy(candidate+offset,payload,length);
    memset(candidate+0x60,0,4);
    uint32_t check=~settings_user_checksum(candidate);memcpy(candidate+0x60,&check,4);
    uint32_t status=settings_user_persist(candidate);
    if(!status){memcpy(eeprom+0x60,candidate+0x60,96);memcpy(settings_cache+0x60,candidate+0x60,96);}
    ReleaseSRWLockExclusive(&eeprom_lock);
    SecureZeroMemory(payload,sizeof(payload));SecureZeroMemory(candidate,sizeof(candidate));
    g_eax=status;
}

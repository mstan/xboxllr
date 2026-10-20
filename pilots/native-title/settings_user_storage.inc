/* Private native storage owns only the 96-byte mutable user section. The
 * supplied console EEPROM, factory region and encrypted keys are never saved
 * over. Atomic replacement retains the previous record on a failed write. */
enum{SETTINGS_USER_RECORD_BYTES=168};
static int settings_user_storage_bound;
static void settings_user_record(unsigned char record[SETTINGS_USER_RECORD_BYTES],const unsigned char snapshot[256])
{
    memcpy(record,"XLLRUSR1",8);memcpy(record+8,settings_region_boot_identity,32);memcpy(record+40,snapshot+0x60,96);
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,record,136,record+136,32)<0)
        stop("user settings record hash failed",29);
}
static uint32_t settings_user_persist(const unsigned char candidate[256])
{
    if(!settings_user_storage_bound)return 0xc0000184u;
    unsigned char record[SETTINGS_USER_RECORD_BYTES];settings_user_record(record,candidate);
    HANDLE file;IO_STATUS_BLOCK io={0};
    static LONG serial;WCHAR temporary[96];
    swprintf(temporary,96,L"user-settings.%lu.%ld.new.aot-private.bin",GetCurrentProcessId(),InterlockedIncrement(&serial));
    uint32_t status=storage_create_host(storage_parent,temporary,
        FILE_GENERIC_WRITE|DELETE,0,0x60,2,FILE_ATTRIBUTE_NORMAL,NULL,&file,&io); /* CREATE, synchronous, non-directory */
    if(status){SecureZeroMemory(record,sizeof(record));return status;}
    DWORD count=0;BOOL ok=WriteFile(file,record,sizeof(record),&count,NULL) && count==sizeof(record) && FlushFileBuffers(file);
    DWORD error=ok?0:GetLastError();SecureZeroMemory(record,sizeof(record));
    if(ok){
        /* Rename relative to the already validated local storage owner. */
        const WCHAR name[]=L"user-settings.aot-private.bin";
        unsigned char bytes[sizeof(FILE_RENAME_INFO)+sizeof(name)]={0};
        FILE_RENAME_INFO *rename=(FILE_RENAME_INFO*)bytes;
        rename->ReplaceIfExists=TRUE;rename->RootDirectory=storage_parent;rename->FileNameLength=sizeof(name)-2;
        memcpy(rename->FileName,name,sizeof(name));
        status=(uint32_t)media_nt_set(file,&io,rename,sizeof(bytes),10); /* FileRenameInformation, relative handle */
        ok=status==0;error=ok?0:ERROR_WRITE_FAULT;
    }
    if(!ok){FILE_DISPOSITION_INFO disposition={TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));}
    if(!ok && pilot_verbose)fprintf(stderr,"[USER SETTINGS] atomic persistence failed win32=%lu\n",error);
    CloseHandle(file);
    return ok?0:status?status:error==ERROR_DISK_FULL?0xc000007fu:error==ERROR_ACCESS_DENIED?0xc0000022u:0xc0000185u;
}
static void settings_user_storage_bind(void)
{
    if(!storage_parent || !have_eeprom)return;
    if(InterlockedCompareExchange(&settings_region_boot_phase,0,0)!=2)stop("user settings require boot identity",29);
    HANDLE file;IO_STATUS_BLOCK io={0};
    uint32_t status=media_open_host(storage_parent,L"user-settings.aot-private.bin",FILE_GENERIC_READ,
        FILE_SHARE_READ,0x60,&file,&io);
    if(status==0xc0000034u){settings_user_storage_bound=1;return;}
    if(status)stop("cannot read private user settings",status);
    unsigned char record[SETTINGS_USER_RECORD_BYTES],hash[32];DWORD count=0;
    LARGE_INTEGER length;
    int valid=GetFileSizeEx(file,&length) && length.QuadPart==sizeof(record) &&
        ReadFile(file,record,sizeof(record),&count,NULL) && count==sizeof(record);
    CloseHandle(file);
    valid=valid && !memcmp(record,"XLLRUSR1",8) && !memcmp(record+8,settings_region_boot_identity,32) &&
        BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,record,136,hash,32)>=0 && !memcmp(record+136,hash,32);
    unsigned char snapshot[256];memcpy(snapshot,eeprom,256);if(valid)memcpy(snapshot+0x60,record+40,96);
    valid=valid && settings_user_checksum(snapshot)==UINT32_MAX;
    if(!valid)stop("invalid source-bound private user settings",29);
    AcquireSRWLockExclusive(&eeprom_lock);memcpy(eeprom+0x60,snapshot+0x60,96);settings_cache_valid=0;ReleaseSRWLockExclusive(&eeprom_lock);
    SecureZeroMemory(record,sizeof(record));SecureZeroMemory(snapshot,sizeof(snapshot));SecureZeroMemory(hash,sizeof(hash));
    settings_user_storage_bound=1;
}

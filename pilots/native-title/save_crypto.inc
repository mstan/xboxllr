#include "sha1.h"
#ifndef PILOT_SAVE_KEYS_FILE
#define PILOT_SAVE_KEYS_FILE ""
#endif
/* Data exports have distinct, process-owned guest storage. Key material comes
 * only from the owner's source-bound offline input, never donor zero stubs. */
static uint32_t save_key_import(unsigned ordinal)
{
    static int loaded;
    const uint32_t base=PILOT_KERNEL_DATA_BASE+0x6000u;
    if(!loaded) {
        unsigned char blob[360],digest[32];
        const char *path=getenv("XLLE_PILOT_SAVE_KEYS_FILE");
        if(!path || !*path)path=PILOT_SAVE_KEYS_FILE;
        FILE *file=fopen(path,"rb");
        if(!file)stop("missing private signing inputs; run tools/native_save_keys.py",ordinal);
        if(fread(blob,1,sizeof(blob),file)!=sizeof(blob) || fgetc(file)!=EOF || fclose(file))
            stop("invalid private signing input size",ordinal);
        if(memcmp(blob,"XLLEKEY1",8))stop("invalid private signing input version",ordinal);
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,image_bytes,(ULONG)image_size,digest,32)<0 ||
           memcmp(blob+8,digest,32))stop("signing input belongs to another XBE",ordinal);
        if(have_eeprom && (BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,eeprom,256,digest,32)<0 ||
                          memcmp(blob+40,digest,32)))stop("signing input belongs to another EEPROM",ordinal);
        commit(base,4096);memcpy(memory+base,blob+72,288);
        SecureZeroMemory(blob,sizeof(blob));loaded=1;
        fprintf(stderr,"[SAVE SIGNING] source-bound title and console keys; values suppressed\n");
    }
    return base+(ordinal==323?0:ordinal==325?16:32);
}
static void save_crypto_kernel(unsigned ordinal)
{
    uint32_t context=MEM32(g_esp+4);
    if(!context || (uint64_t)context+116>0x100000000ull)stop("invalid SHA context",context);
    if(ordinal==335){pilot_sha_init(memory+context);g_esp+=8;}
    else if(ordinal==336) {
        uint32_t input=MEM32(g_esp+8),length=MEM32(g_esp+12);
        if((!input && length) || (uint64_t)input+length>0x100000000ull)stop("invalid SHA input",input);
        pilot_sha_update(memory+context,memory+input,length);g_esp+=16;
    } else {
        uint32_t output=MEM32(g_esp+8);
        if(!output || (uint64_t)output+20>0x100000000ull)stop("invalid SHA digest",output);
        pilot_sha_final(memory+context,memory+output);g_esp+=12;
    }
}

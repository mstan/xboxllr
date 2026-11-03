/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bcrypt.h"
#include <openssl/evp.h>
struct Hash {EVP_MD_CTX *context;unsigned bytes;int finished;};
LONG BCryptCreateHash(BCRYPT_ALG_HANDLE algorithm,BCRYPT_HASH_HANDLE *out,unsigned char *storage,ULONG storage_bytes,unsigned char *key,ULONG key_bytes,ULONG flags)
{
    if(!out || storage || storage_bytes || key || key_bytes || flags)return (LONG)0xc00000bbu;
    const EVP_MD *digest=algorithm==BCRYPT_SHA1_ALG_HANDLE?EVP_sha1():algorithm==BCRYPT_SHA256_ALG_HANDLE?EVP_sha256():NULL;
    if(!digest)return (LONG)0xc00000bbu;
    struct Hash *hash=calloc(1,sizeof(*hash));if(!hash)return (LONG)0xc0000017u;
    hash->context=EVP_MD_CTX_new();hash->bytes=(unsigned)EVP_MD_size(digest);
    if(!hash->context || !EVP_DigestInit_ex(hash->context,digest,NULL)){EVP_MD_CTX_free(hash->context);free(hash);return (LONG)0xc0000001u;}
    *out=hash;return 0;
}
LONG BCryptHashData(BCRYPT_HASH_HANDLE opaque,unsigned char *bytes,ULONG count,ULONG flags)
{struct Hash *hash=opaque;return hash && !hash->finished && !flags && (!count || bytes) && EVP_DigestUpdate(hash->context,bytes,count)?0:(LONG)0xc000000du;}
LONG BCryptFinishHash(BCRYPT_HASH_HANDLE opaque,unsigned char *out,ULONG count,ULONG flags)
{
    struct Hash *hash=opaque;unsigned actual=0;
    if(!hash || hash->finished || !out || count!=hash->bytes || flags)return (LONG)0xc000000du;
    hash->finished=1;return EVP_DigestFinal_ex(hash->context,out,&actual) && actual==count?0:(LONG)0xc0000001u;
}
LONG BCryptDestroyHash(BCRYPT_HASH_HANDLE opaque)
{struct Hash *hash=opaque;if(!hash)return (LONG)0xc000000du;EVP_MD_CTX_free(hash->context);free(hash);return 0;}
LONG BCryptHash(BCRYPT_ALG_HANDLE algorithm,unsigned char *key,ULONG key_bytes,unsigned char *bytes,ULONG count,unsigned char *out,ULONG output_bytes)
{
    BCRYPT_HASH_HANDLE hash=NULL;LONG result=BCryptCreateHash(algorithm,&hash,NULL,0,key,key_bytes,0);
    if(result>=0)result=BCryptHashData(hash,bytes,count,0);
    if(result>=0)result=BCryptFinishHash(hash,out,output_bytes,0);
    if(hash)BCryptDestroyHash(hash);return result;
}

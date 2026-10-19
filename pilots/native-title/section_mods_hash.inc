/* Windows transport for the portable section-mod validator. */
static int section_mod_hash(void *context,enum PilotSectionHash kind,const void *data,
                            uint32_t bytes,unsigned char *out)
{
    (void)context;
    if(kind==PILOT_SECTION_SHA256)
        return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)data,bytes,out,32)>=0;
    if(kind!=PILOT_SECTION_LENGTH_SHA1)return 0;
    BCRYPT_HASH_HANDLE hash=NULL;
    unsigned char prefix[4];
    for(unsigned i=0;i<4;++i)prefix[i]=(unsigned char)(bytes>>(8*i));
    if(BCryptCreateHash(BCRYPT_SHA1_ALG_HANDLE,&hash,NULL,0,NULL,0,0)<0)return 0;
    int ok=BCryptHashData(hash,prefix,4,0)>=0 && BCryptHashData(hash,(PUCHAR)data,bytes,0)>=0 &&
        BCryptFinishHash(hash,out,20,0)>=0;
    BCryptDestroyHash(hash);return ok;
}

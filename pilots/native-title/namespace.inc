/* Guest object-manager links: actual namespace entries and independently owned
 * query handles. Architecture reference: sp00nznet 1409a7d's link table/path
 * resolution. Deleting a name does not destroy an object with open handles. */
static struct { char name[128],target[1024]; unsigned named,refs; } namespace_links[64];
static struct { uint32_t id; unsigned object; } namespace_handles[128];
static uint32_t namespace_next_id=0x60000001u;
static uint32_t namespace_string(uint32_t string,char *out,unsigned capacity)
{
    if (!string) return MEDIA_INVALID;
    unsigned length=MEM16(string);
    if (!length || length>=capacity || length>MEM16(string+2)) return 0xc0000033u;
    memcpy(out,memory+MEM32(string+4),length); out[length]=0;
    for (unsigned i=0;i<length;++i) {
        if (!out[i]) return 0xc0000033u;
        if ((unsigned char)out[i]>=128) stop("non-ASCII object name unsupported",string);
    }
    return 0;
}
static int namespace_find(const char *name)
{
    for (unsigned i=0;i<64;++i) if (namespace_links[i].named && !_stricmp(name,namespace_links[i].name)) return (int)i;
    return -1;
}
static uint32_t namespace_create(const char *name,const char *target)
{
    if (name[0]!='\\' || target[0]!='\\' || !name[1] || !target[1] ||
        strlen(name)>=128 || strlen(target)>=1024) return 0xc0000033u;
    if (namespace_find(name)>=0) return 0xc0000035u;
    for (unsigned i=0;i<64;++i) if (!namespace_links[i].named && !namespace_links[i].refs) {
        strcpy(namespace_links[i].name,name); strcpy(namespace_links[i].target,target);
        namespace_links[i].named=1; return 0;
    }
    return 0xc000009au;
}
static uint32_t namespace_close(uint32_t id)
{
    uint32_t status=MEDIA_BAD_HANDLE;
    AcquireSRWLockExclusive(&media_lock);
    for (unsigned i=0;i<128;++i) if (namespace_handles[i].id && namespace_handles[i].id==id) {
        --namespace_links[namespace_handles[i].object].refs;
        namespace_handles[i].id=0; status=0; break;
    }
    ReleaseSRWLockExclusive(&media_lock); return status;
}
static uint32_t namespace_resolve(const char *input,char output[1024])
{
    if (strlen(input)>=1024) return 0xc0000033u;
    if (input[0] && input[1]==':') snprintf(output,1024,"\\??\\%s",input);
    else strcpy(output,input);
    for (unsigned depth=0;depth<32;++depth) {
        int best=-1; size_t prefix=0;
        for (unsigned i=0;i<64;++i) if (namespace_links[i].named) {
            size_t length=strlen(namespace_links[i].name);
            if (length>prefix && !_strnicmp(output,namespace_links[i].name,length) &&
                (!output[length] || output[length]=='\\')) { best=(int)i; prefix=length; }
        }
        if (best<0) return 0;
        char next[1024]; size_t target=strlen(namespace_links[best].target);
        if (target && namespace_links[best].target[target-1]=='\\' && output[prefix]=='\\') ++prefix;
        size_t tail=strlen(output+prefix);
        if (target+tail>=sizeof(next)) return 0xc0000033u;
        memcpy(next,namespace_links[best].target,target); memcpy(next+target,output+prefix,tail+1);
        strcpy(output,next);
    }
    return 0xc0000039u; /* bounded cycle/depth failure, never invent a destination */
}
static void namespace_kernel(unsigned ordinal)
{
    uint32_t a=MEM32(g_esp+4),b=ordinal==69 ? 0 : MEM32(g_esp+8);
    char name[128],target[1024]; uint32_t status=0;
    AcquireSRWLockExclusive(&media_lock);
    if (ordinal==67 || ordinal==69) {
        status=namespace_string(a,name,sizeof(name));
        if (!status && ordinal==67) {
            status=namespace_string(b,target,sizeof(target));
            if (!status) status=namespace_create(name,target);
            if (!status) fprintf(stderr,"[NAMESPACE] %s -> %s\n",name,target);
        } else if (!status) {
            int at=namespace_find(name);
            if (at<0) status=0xc0000034u;
            else namespace_links[at].named=0;
        }
    } else if (ordinal==203) {
        if (!a || !b) status=MEDIA_INVALID;
        else if (MEM32(b) || (MEM32(b+8)&~0x40u)) stop("symbolic-link object attributes unsupported",203);
        else status=namespace_string(MEM32(b+4),name,sizeof(name));
        if (!status) {
            int at=namespace_find(name); unsigned slot=0;
            while (slot<128 && namespace_handles[slot].id) ++slot;
            if (at<0) status=0xc0000034u;
            else if (slot==128 || namespace_next_id==0x70000000u) status=0xc000009au;
            else {
                namespace_handles[slot].id=namespace_next_id++;
                namespace_handles[slot].object=(unsigned)at;
                ++namespace_links[at].refs; MEM32(a)=namespace_handles[slot].id;
            }
        }
    } else {
        int at=-1; for (unsigned i=0;i<128;++i) if (namespace_handles[i].id && namespace_handles[i].id==a) at=(int)namespace_handles[i].object;
        if (at<0) status=MEDIA_BAD_HANDLE;
        else if (!b) status=MEDIA_INVALID;
        else {
            unsigned length=(unsigned)strlen(namespace_links[at].target);
            uint32_t required=MEM32(g_esp+12);
            if (required) MEM32(required)=length;
            unsigned copied=length<MEM16(b+2)?length:MEM16(b+2);
            if(copied)memcpy(memory+MEM32(b+4),namespace_links[at].target,copied);
            MEM16(b)=(uint16_t)copied;
            if(copied<length)status=0xc0000023u;
        }
    }
    ReleaseSRWLockExclusive(&media_lock);
    g_eax=status; g_esp+=ordinal==69 ? 8 : ordinal==215 ? 16 : 12;
}

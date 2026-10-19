/* Returns a validated relative ASCII path. No host path, traversal, alternate
 * stream or drive-name interpretation is available to guest input. */
static uint32_t media_path(uint32_t attributes,WCHAR path[1024],HANDLE *parent,unsigned *partition)
{
    if (!attributes || !MEM32(attributes+4)) return MEDIA_INVALID;
    if (MEM32(attributes+8)&~0x40u) stop("file object attributes unsupported",MEM32(attributes+8));
    uint32_t string=MEM32(attributes+4),length=MEM16(string),buffer=MEM32(string+4);
    if (!length || length>=1024 || length>MEM16(string+2)) return 0xc0000033u;
    char name[1024]; memcpy(name,memory+buffer,length); name[length]=0;
    if (strlen(name)!=length) return 0xc0000033u;
    uint32_t root=MEM32(attributes);
    char resolved[1024]; const char *part=name;
    if (root && root!=0xfffffffdu) {
        *parent=media_handle(root);
        if (!*parent) return MEDIA_BAD_HANDLE;
        *partition=media_partition(root);
    } else {
        /* nxdk ObDosDevicesDirectory() is the object-directory pseudo handle
         * -3, not a file handle. Resolve relative names inside guest \??. */
        char dos_name[1024]; const char *input=name;
        if(root==0xfffffffdu) {
            if(name[0]=='\\' || length+4>=sizeof(dos_name))return 0xc0000033u;
            snprintf(dos_name,sizeof(dos_name),"\\??\\%s",name);input=dos_name;
        }
        uint32_t status;
#if defined(PILOT_OPTICAL_EMPTY_NATIVE) && PILOT_OPTICAL_EMPTY_NATIVE
        status=namespace_resolve_checked(input,resolved,media_guard_empty_optical_path);
#else
        status=namespace_resolve(input,resolved);
#endif
        if (status) return status;
        part=resolved;
        *parent=media_root;
#ifdef PILOT_MEDIA_HOST
        const size_t content_prefix=strlen(PILOT_TITLE_DEVICE);
        if(!_strnicmp(part,PILOT_TITLE_DEVICE,content_prefix) &&
           (!part[content_prefix] || part[content_prefix]=='\\')) {
            part+=content_prefix;if(*part=='\\')++part;
            /* Installed content is read-only, independently of writable save
             * partitions. A drive letter is profile data, not a title branch. */
            *partition=0;
        } else
#endif
        if (!_strnicmp(part,"\\Device\\CdRom0\\",15)) part+=15;
        else if (!_strnicmp(part,"\\Device\\Harddisk0\\Partition",27)) {
            unsigned number=(unsigned)(part[27]-'0');
            if (number<1 || number>5 || (part[28] && part[28]!='\\'))
                stop("unmounted raw/extended HDD partition",202);
            *parent=storage_roots[number]; *partition=number;
            if (!*parent) stop("HDD access requires a mounted local storage directory",number);
            part+=28; if (*part=='\\') ++part;
        }
        else if (!_stricmp(part,"\\Device\\CdRom0"))
            stop("raw optical device requires optical media/authentication model",202);
        else return 0xc000003au;
        if (!*parent) return MEDIA_NO_MEDIA;
    }
    const char *component=part;
    for (unsigned i=0;;++i) {
        unsigned char c=(unsigned char)part[i];
        if (c>=128) stop("non-ASCII guest media path needs Xbox character mapping",202);
        if (c=='/' || c==':' || c=='*' || c=='?' || (c && c<32)) return 0xc0000033u;
        if (!c || c=='\\') {
            size_t n=part+i-component;
            if ((n==1 && component[0]=='.') || (n==2 && component[0]=='.' && component[1]=='.') ||
                (!n && c) || (n && (component[n-1]=='.' || component[n-1]==' '))) return 0xc0000033u;
            component=part+i+1;
        }
        path[i]=c;
        if (!c) break;
    }
    return 0;
}

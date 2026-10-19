/* Called under media_lock. Only an explicitly absent virtual optical device
 * is admitted; this never dismounts a content root or a guest file handle. */
static void media_guard_empty_optical_path(const char *resolved)
{
    if(!_strnicmp(resolved,"\\Device\\CdRom0",14) && (!resolved[14] || resolved[14]=='\\'))
        stop("empty optical owner has no qualified file device",202);
}
static uint32_t media_dismount_empty_optical(uint32_t string)
{
    if(!kernel_optical_is_empty())stop("optical namespace has no empty owner",91);
#ifdef PILOT_MEDIA_HOST
    const char *device=PILOT_TITLE_DEVICE;
    if(!_strnicmp(device,"\\Device\\CdRom0",14) && (!device[14] || device[14]=='\\'))
        stop("empty optical policy conflicts with title content owner",91);
#else
    stop("empty optical dismount requires explicit host namespace ownership",91);
#endif
    if(string) {
        if(string>UINT32_MAX-7)stop("invalid dismount name descriptor",91);
        uint32_t length=MEM16(string),buffer=MEM32(string+4);
        if(length && (!buffer || (uint64_t)buffer+length>UINT64_C(0x100000000)))
            stop("invalid dismount name buffer",91);
    }
    char name[1024],resolved[1024];
    uint32_t status=namespace_string(string,name,sizeof(name));
    if(!status)status=namespace_resolve(name,resolved);
    if(status)return status;
    /* The original caller also probes this absent DOS object name. Do not
     * create an alias: a real registered link has already been resolved, so
     * mounted/remapped targets still reach the owner guard below. */
    if(_stricmp(resolved,"\\Device\\CdRom0") && _stricmp(resolved,"\\??\\CdRom0:"))
        stop("dismount requires a qualified volume owner",91);
    return 0xc0000034u; /* No device or unresolved optical DOS object exists. */
}

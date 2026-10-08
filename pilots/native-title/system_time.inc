/* Shared NT time conversion. Original kernel fixtures establish the 8 signed
 * 16-bit fields and 100ns epoch. Phobos also delegates calendar work to host
 * APIs; this independent adapter calls the matching NT routines directly. */
static void system_time_fields(unsigned ordinal)
{
    uint32_t source=MEM32(g_esp+4),destination=MEM32(g_esp+8);
    if(!source || !destination || source>UINT32_MAX-15 || destination>UINT32_MAX-15)
        stop("invalid time conversion range",ordinal);
    LARGE_INTEGER time;int16_t fields[8];
    HMODULE module=GetModuleHandleW(L"ntdll.dll");
    if(ordinal==305) {
        typedef VOID (NTAPI *ToFields)(const LARGE_INTEGER*,void*);
        ToFields convert=(ToFields)(void*)GetProcAddress(module,"RtlTimeToTimeFields");
        if(!convert)stop("native time-to-fields service unavailable",ordinal);
        memcpy(&time,memory+source,8);convert(&time,fields);memcpy(memory+destination,fields,16);
    } else {
        typedef BOOLEAN (NTAPI *ToTime)(const void*,LARGE_INTEGER*);
        ToTime convert=(ToTime)(void*)GetProcAddress(module,"RtlTimeFieldsToTime");
        if(!convert)stop("native fields-to-time service unavailable",ordinal);
        memcpy(fields,memory+source,16);g_eax=convert(fields,&time);
        if(g_eax)memcpy(memory+destination,&time,8);
    }
    g_esp+=12;
}

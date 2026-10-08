/* Phobos hle_d3d8_state.c caches diagnostic environment lookups, including
 * the disabled case. Drawing must not search the environment for every
 * primitive. These process configuration strings are owned until exit.
 * Calls are serialized by graphics_lock. The capture trigger FILE remains
 * live so a configured diagnostic can still start after scene loading. */
enum GraphicsDiagnostic {
    GRAPHICS_DIAGNOSTIC_DRAW,
    GRAPHICS_DIAGNOSTIC_FEEDBACK,
    GRAPHICS_DIAGNOSTIC_FVF,
    GRAPHICS_DIAGNOSTIC_FRAME,
    GRAPHICS_DIAGNOSTIC_FEEDBACK_TRACE,
    GRAPHICS_DIAGNOSTIC_COUNT
};
static const char *graphics_diagnostic(enum GraphicsDiagnostic which)
{
    static struct {const char *name;char *value;int loaded;} options[]={
        {"XLLE_PILOT_DRAW_CAPTURE",NULL,0},
        {"XLLE_PILOT_FEEDBACK_CAPTURE",NULL,0},
        {"XLLE_PILOT_FVF_CAPTURE",NULL,0},
        {"XLLE_PILOT_FRAME_CAPTURE",NULL,0},
        {"XLLE_PILOT_FEEDBACK_TRACE",NULL,0}
    };
    static int cache=-1;
    if(cache<0) {const char *setting=getenv("XLLE_PILOT_DIAGNOSTIC_CACHE");cache=!(setting && !strcmp(setting,"0"));}
    if(!cache)return getenv(options[which].name); /* Separate-run timing reference. */
    if(!options[which].loaded) {
        const char *value=getenv(options[which].name);
        if(value && !(options[which].value=_strdup(value)))stop("diagnostic setting allocation failed",which);
        options[which].loaded=1;
    }
    return options[which].value;
}

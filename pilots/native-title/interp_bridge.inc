/* Guest interpreter fallback for code generation missed (included by runtime.c).
 *
 * Generated code calls recomp_bridge_call() where it used to stop: an
 * unresolved indirect call/jump, or a direct call to an undiscovered or
 * untranslated function.  The bridge runs the original guest function in the
 * portable interpreter (src/cpu/recomp_interp_bridge.c) and returns to the
 * generated caller.  Every newly bridged address is reported once on stderr
 * and appended to the heal manifest; the coverage line is printed on every
 * stop.  Native code runs first; the interpreter is the always-available
 * fallback (owner decision 2026-11-18).  XLLE_PILOT_INTERPRETER=strict
 * restores stop-on-miss for qualification runs.
 */
#include "recomp_interp_bridge.h"

#ifndef PILOT_SEH_HELPER_COUNT
/* A preparation from before the SEH helper table cannot bridge safely: the
 * interpreter would call native SEH helpers that exchange EBP only through
 * generated callers' g_seh_ebp bookkeeping.  Such builds stay strict. */
#define PILOT_INTERP_NO_SEH_TABLE 1
#define PILOT_SEH_HELPER_COUNT 0u
static const unsigned pilot_seh_helpers[] = { 0u };
#endif

static int interp_strict = 0;
static FILE *interp_manifest;
static SRWLOCK interp_manifest_lock = SRWLOCK_INIT;

static recomp_bridge_func_t interp_lookup(uint32_t va)
{
    recomp_func_t fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn) fn = recomp_lookup_kernel(va);
    return fn;
}

static int interp_seh_helper(uint32_t va)
{
    unsigned lo = 0, hi = PILOT_SEH_HELPER_COUNT;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (pilot_seh_helpers[mid] == va) return 1;
        if (pilot_seh_helpers[mid] < va) lo = mid + 1; else hi = mid;
    }
    return 0;
}

static uint64_t interp_rdtsc(void) { return xbox_ReadTimeStampCounter(); }

static void interp_fatal(const char *reason, uint32_t va, const XlleX86Result *result)
{
    fprintf(stderr, "[INTERP FAULT] %s: fault=%d detail=%d at %08x after %llu interpreted instructions\n",
            reason, result->fault, result->fault_detail, result->fault_eip,
            (unsigned long long)result->retired);
    stop(reason, va);
}

static void interp_first_entry(uint32_t va, unsigned kind, uint32_t return_address)
{
    static const char *kinds[] = { "?", "indirect call", "indirect tail jump", "undiscovered function", "untranslated function",
                                   "forced (qualification)" };
    fprintf(stderr, "[INTERP] first interpreted entry %08x (%s) returning to %08x\n",
            va, kinds[kind < 6 ? kind : 0], return_address);
    if (interp_manifest) {
        AcquireSRWLockExclusive(&interp_manifest_lock);
        fprintf(interp_manifest, "{\"va\":\"0x%08X\",\"kind\":%u,\"first_return\":\"0x%08X\",\"xbe\":\"%s\",\"core\":\"%s\"}\n",
                va, kind, return_address, PILOT_XBE_SHA256, xlle_x86_interp_identity());
        fflush(interp_manifest);
        ReleaseSRWLockExclusive(&interp_manifest_lock);
    }
}

static void interp_progress(uint32_t va, uint32_t entries, uint64_t instructions, uint64_t host_ns)
{
    fprintf(stderr, "[INTERP] %08x interpreted %u times: %llu guest instructions, %.3f ms host time (%.2f us per call)\n",
            va, entries, (unsigned long long)instructions, (double)host_ns / 1e6, (double)host_ns / 1e3 / entries);
}

int recomp_bridge_call(uint32_t va, unsigned kind)
{
    if (interp_strict)
        return 0;
    return recomp_interp_bridge_call(va, kind);
}

static void interp_report(void)
{
    char line[256];
    recomp_interp_bridge_coverage_line(line, sizeof line);
    fprintf(stderr, "[INTERP] %s%s\n", line, interp_strict ? " (strict: interpreter disabled)" : "");
}

/* Folded into the session record/replay identity: a bridge-mode recording
 * and a strict replay are different simulations. */
static const char *interp_profile(void)
{
    return interp_strict ? "xlle-interp:strict" : "xlle-interp:bridge:" XLLE_X86_INTERP_IDENTITY_STRING;
}

static void interp_bridge_init(void)
{
    const char *mode = getenv("XLLE_PILOT_INTERPRETER");
    const char *manifest = getenv("XLLE_PILOT_INTERPRETER_MANIFEST");
    RecompInterpBridgeConfig config;
    interp_strict = mode && !strcmp(mode, "strict");
#ifdef PILOT_INTERP_NO_SEH_TABLE
    if (!interp_strict)
        fprintf(stderr, "[INTERP] this generation has no SEH helper table; interpreter disabled (regenerate)\n");
    interp_strict = 1;
#endif
    if (mode && *mode && strcmp(mode, "strict") && strcmp(mode, "bridge"))
        stop("XLLE_PILOT_INTERPRETER must be strict or bridge", 0);
    if (manifest && *manifest && !interp_strict) {
        interp_manifest = fopen(manifest, "a");
        if (!interp_manifest) stop("cannot open interpreter heal manifest", 0);
    }
    memset(&config, 0, sizeof config);
    config.lookup = interp_lookup;
    config.interpret_only = interp_seh_helper;
    config.cpuid = recomp_cpuid;
    config.rdtsc = interp_rdtsc;
    config.fatal = interp_fatal;
    config.first_entry = interp_first_entry;
    config.progress = interp_progress;
    config.strict = interp_strict;
    recomp_interp_bridge_configure(&config);
    fprintf(stderr, "[INTERP] guest interpreter %s (%s, %u SEH helpers)\n",
            interp_strict ? "strict" : "bridge", xlle_x86_interp_identity(), PILOT_SEH_HELPER_COUNT);
}

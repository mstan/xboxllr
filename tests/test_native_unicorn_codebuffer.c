/* Authored execution over real Windows lazy translation-buffer commits. */
#include <unicorn/unicorn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "FAIL codebuffer line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define UC(c) REQUIRE((c) == UC_ERR_OK)
#define BASE 0x100000u
#define DATA 0x400000u
#define BLOCKS 50000u
#define STRIDE 16u
static unsigned edge_first;
static void edge(uc_engine *cpu, uc_tb *cur, uc_tb *previous, void *opaque)
{
    (void)cpu; (void)previous; (void)opaque;
    if (cur->pc == BASE + STRIDE) ++edge_first;
}
static void store32(unsigned char *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (unsigned char)(value >> (i * 8));
}
#ifdef _WIN32
static uintptr_t reserve_base, reserve_end;
static unsigned faults, tail_faults;
static uint32_t expected_size;
/* Observe only: this handler never commits memory or handles an exception. */
static LONG CALLBACK observe_commit(PEXCEPTION_POINTERS exception)
{
    PEXCEPTION_RECORD record = exception->ExceptionRecord;
    MEMORY_BASIC_INFORMATION info;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t address = (uintptr_t)record->ExceptionInformation[1];
    if (!VirtualQuery((void *)address, &info, sizeof(info)) || info.State != MEM_RESERVE ||
        info.AllocationProtect != PAGE_EXECUTE_READWRITE) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t first = (uintptr_t)info.AllocationBase, end = first;
    while (VirtualQuery((void *)end, &info, sizeof(info)) && (uintptr_t)info.AllocationBase == first)
        end = (uintptr_t)info.BaseAddress + info.RegionSize;
    if (!reserve_base) { reserve_base = first; reserve_end = end; }
    REQUIRE(first == reserve_base && end == reserve_end && end - first == expected_size);
    ++faults;
    if (end - address < 4u * 1024u * 1024u) ++tail_faults;
    printf("COMMIT observation %u offset=%llu remaining=%llu reserved=%llu\n", faults,
        (unsigned long long)(address - first), (unsigned long long)(end - address),
        (unsigned long long)(end - first));
    fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI unhandled(PEXCEPTION_POINTERS exception)
{
    fprintf(stderr, "UNHANDLED codebuffer exception=%08lx address=%p target=%p last_error=%lu faults=%u tail_faults=%u\n",
        exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress,
        (void *)exception->ExceptionRecord->ExceptionInformation[1], GetLastError(), faults, tail_faults);
    fflush(stderr);
    ExitProcess(86); /* Preserve a finite red witness without a Windows error UI. */
}
static void check_committed(void)
{
    MEMORY_BASIC_INFORMATION info;
    REQUIRE(reserve_base && faults && tail_faults);
    uintptr_t at = reserve_base;
    size_t committed = 0;
    while (at < reserve_end) {
        REQUIRE(VirtualQuery((void *)at, &info, sizeof(info)) && (uintptr_t)info.AllocationBase == reserve_base);
        REQUIRE((uintptr_t)info.BaseAddress == at && info.RegionSize <= reserve_end - at);
        REQUIRE(info.State == MEM_COMMIT);
        /* tcg_region_init may protect the final, unused host page. */
        SYSTEM_INFO system; GetSystemInfo(&system);
        REQUIRE(info.Protect == PAGE_EXECUTE_READWRITE ||
            (info.Protect == PAGE_NOACCESS && at == reserve_end - system.dwPageSize &&
             info.RegionSize == system.dwPageSize));
        committed += info.RegionSize;
        at += info.RegionSize;
    }
    REQUIRE(committed == expected_size);
    printf("COMMITTED within reservation=%llu faults=%u tail_faults=%u\n",
        (unsigned long long)committed, faults, tail_faults);
}
#endif
static void lifetime(uint32_t size)
{
    uc_engine *cpu = NULL;
#ifdef _WIN32
    reserve_base = reserve_end = 0; faults = tail_faults = 0; expected_size = size;
#endif
    edge_first = 0;
    printf("BEGIN codebuffer bytes=%u blocks=%u\n", size, BLOCKS); fflush(stdout);
    UC(uc_open(UC_ARCH_X86, UC_MODE_32, &cpu));
    UC(uc_ctl_set_tcg_buffer_size(cpu, size));
    UC(uc_mem_map(cpu, BASE, 0x100000, UC_PROT_ALL));
    UC(uc_mem_map(cpu, DATA, 0x1000, UC_PROT_ALL));
    unsigned char *code = (unsigned char *)malloc(BLOCKS * STRIDE);
    REQUIRE(code);
    memset(code, 0x90, BLOCKS * STRIDE);
    for (unsigned i = 0; i < BLOCKS; ++i) {
        unsigned char *p = code + i * STRIDE;
        p[0] = 0x40; /* INC EAX */
        p[1] = 0x83; p[2] = 0x05; store32(p + 3, DATA + 16); p[7] = 1;
        p[8] = 0xe9; store32(p + 9, STRIDE - 13); /* JMP next owned block */
    }
    UC(uc_mem_write(cpu, BASE, code, BLOCKS * STRIDE)); free(code);
    unsigned char guarded[36]; memset(guarded, 0xa5, sizeof(guarded)); store32(guarded + 16, 0);
    UC(uc_mem_write(cpu, DATA, guarded, sizeof(guarded)));
    int ids[] = {UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP};
    uint32_t canaries[] = {0x12345678, 0x23456789, 0x3456789a, 0x456789ab, DATA + 0x800};
    for (unsigned i = 0; i < 5; ++i) UC(uc_reg_write(cpu, ids[i], &canaries[i]));
    uint32_t zero = 0; UC(uc_reg_write(cpu, UC_X86_REG_EAX, &zero));
    uc_hook hook; UC(uc_hook_add(cpu, &hook, UC_HOOK_EDGE_GENERATED, (void *)edge, NULL, 1, 0));
    for (unsigned pass = 1; pass <= 3; ++pass) {
        if (pass == 3) UC(uc_ctl_flush_tb(cpu));
        UC(uc_emu_start(cpu, BASE, BASE + BLOCKS * STRIDE, 10000000, 0));
        uint32_t value;
        UC(uc_reg_read(cpu, UC_X86_REG_EAX, &value)); REQUIRE(value == BLOCKS * pass);
        UC(uc_reg_read(cpu, UC_X86_REG_EIP, &value)); REQUIRE(value == BASE + BLOCKS * STRIDE);
        for (unsigned i = 0; i < 5; ++i) { UC(uc_reg_read(cpu, ids[i], &value)); REQUIRE(value == canaries[i]); }
        UC(uc_mem_read(cpu, DATA, guarded, sizeof(guarded)));
        memcpy(&value, guarded + 16, sizeof(value)); REQUIRE(value == BLOCKS * pass);
        for (unsigned i = 0; i < sizeof(guarded); ++i) if (i < 16 || i >= 20) REQUIRE(guarded[i] == 0xa5);
        printf("EXECUTED pass=%u eax=%u memory=%u first_edge_generations=%u\n", pass, value, value, edge_first);
        REQUIRE(edge_first >= pass); /* Prior first edge was retired by real cache pressure. */
    }
#ifdef _WIN32
    check_committed();
#endif
    UC(uc_close(cpu));
}
int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled);
    PVOID observer = AddVectoredExceptionHandler(1, observe_commit); REQUIRE(observer);
#endif
    if (argc == 2) lifetime((uint32_t)strtoul(argv[1], NULL, 10) * 1024u * 1024u);
    else { lifetime(1024u * 1024u); lifetime(5u * 1024u * 1024u); lifetime(8u * 1024u * 1024u); }
#ifdef _WIN32
    REQUIRE(RemoveVectoredExceptionHandler(observer));
#endif
    puts("PASS: minimum, partial-tail and chunk-aligned code buffers commit only their reservation; actual authored execution, automatic retirement, explicit flush and GPR/memory ownership remain correct");
    return 0;
}

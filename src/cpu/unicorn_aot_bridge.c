#include "unicorn_aot_bridge.h"

/*
 * This source is built into the exact Unicorn/QEMU revision pinned by xlle's
 * CMake. Keep all private-structure use here so the rest of xlle only sees the
 * small C ABI in unicorn_aot_bridge.h.
 */
#include "uc_priv.h"
#include "cpu.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"
#include "translate-all.h"

static _Thread_local void* xlle_restore_engine;
static _Thread_local xlle_unicorn_restore_callback xlle_restore_callback;
static _Thread_local void* xlle_restore_opaque;

void xlle_unicorn_set_restore_callback(void* engine, xlle_unicorn_restore_callback callback,
                                        void* opaque) {
    xlle_restore_engine = engine;
    xlle_restore_callback = callback;
    xlle_restore_opaque = opaque;
}
void xlle_unicorn_record_restore(void* engine, uint64_t pc, uint32_t size,
                                 uint32_t total, uint32_t completed, int exiting) {
    if (exiting && engine == xlle_restore_engine && xlle_restore_callback) {
        const xlle_unicorn_restore_record record = {pc, size, total, completed};
        xlle_restore_callback(xlle_restore_opaque, &record);
    }
}
int xlle_unicorn_irq_inhibited(void* engine) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    if (!uc || !uc->cpu || !uc->cpu->env_ptr) return 1;
    return (((CPUX86State*)uc->cpu->env_ptr)->hflags & HF_INHIBIT_IRQ_MASK) != 0;
}

static int xlle_aot_seg_flat_code32(const SegmentCache* seg) {
    const uint32_t required = DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                              DESC_R_MASK | DESC_B_MASK | DESC_G_MASK;
    const uint32_t forbidden = DESC_L_MASK | DESC_C_MASK;
    if (!seg) {
        return 0;
    }
    if (seg->base != 0 || seg->limit != 0xffffffffu) {
        return 0;
    }
    if ((seg->flags & required) != required) {
        return 0;
    }
    if ((seg->flags & forbidden) != 0) {
        return 0;
    }
    return ((seg->flags & DESC_DPL_MASK) == 0) ? 1 : 0;
}

static int xlle_aot_seg_flat_data32(const SegmentCache* seg) {
    const uint32_t required = DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                              DESC_B_MASK | DESC_G_MASK;
    const uint32_t forbidden = DESC_CS_MASK | DESC_E_MASK;
    if (!seg) {
        return 0;
    }
    if (seg->base != 0 || seg->limit != 0xffffffffu) {
        return 0;
    }
    if ((seg->flags & required) != required) {
        return 0;
    }
    if ((seg->flags & forbidden) != 0) {
        return 0;
    }
    return ((seg->flags & DESC_DPL_MASK) == 0) ? 1 : 0;
}

int xlle_unicorn_aot_context_ok(void* engine) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    CPUState* cpu;
    CPUX86State* env;
    uint32_t rejected_hflags;
    uint32_t rejected_hflags2;
    unsigned i;

    if (!uc || uc->arch != UC_ARCH_X86 || !uc->cpu || !uc->cpu->env_ptr || uc->snapshot_level) {
        return 0;
    }
    if ((uc->mode & UC_MODE_32) == 0 || (uc->mode & (UC_MODE_16 | UC_MODE_64)) != 0) {
        return 0;
    }

    cpu = uc->cpu;
    env = (CPUX86State*)cpu->env_ptr;

    if ((env->cr[0] & CR0_PE_MASK) == 0) {
        return 0;
    }
#ifdef CR4_PAE_MASK
    if ((env->cr[4] & CR4_PAE_MASK) != 0) {
        return 0;
    }
#endif
#ifdef CR4_SMAP_MASK
    if ((env->cr[4] & CR4_SMAP_MASK) != 0) {
        return 0;
    }
#endif
#ifdef CR4_PKE_MASK
    if ((env->cr[4] & CR4_PKE_MASK) != 0) {
        return 0;
    }
#endif

    rejected_hflags = HF_INHIBIT_IRQ_MASK | HF_ADDSEG_MASK | HF_TF_MASK |
                      HF_LMA_MASK | HF_CS64_MASK | HF_RF_MASK | HF_VM_MASK |
                      HF_AC_MASK | HF_SMM_MASK | HF_SVME_MASK | HF_GUEST_MASK |
                      HF_SMAP_MASK | HF_IOBPT_MASK | HF_MPX_EN_MASK |
                      HF_MPX_IU_MASK;
    if ((env->hflags & rejected_hflags) != 0) {
        return 0;
    }
    if ((env->hflags & HF_CPL_MASK) != 0) {
        return 0;
    }
    if ((env->hflags & (HF_PE_MASK | HF_CS32_MASK | HF_SS32_MASK)) !=
        (HF_PE_MASK | HF_CS32_MASK | HF_SS32_MASK)) {
        return 0;
    }

    rejected_hflags2 = HF2_NMI_MASK | HF2_VINTR_MASK | HF2_SMM_INSIDE_NMI_MASK |
                       HF2_MPX_PR_MASK | HF2_NPT_MASK;
    if ((env->hflags2 & rejected_hflags2) != 0) {
        return 0;
    }

    if ((env->eflags & (TF_MASK | VM_MASK | RF_MASK | AC_MASK | NT_MASK | VIF_MASK | VIP_MASK)) != 0) {
        return 0;
    }
    if (cpu->singlestep_enabled || cpu->watchpoint_hit) {
        return 0;
    }
    if (!QTAILQ_EMPTY(&cpu->breakpoints) || !QTAILQ_EMPTY(&cpu->watchpoints)) {
        return 0;
    }
    if (cpu->interrupt_request != 0 || cpu->halted || cpu->stop || cpu->stopped ||
        cpu->exit_request || cpu->crash_occurred) {
        return 0;
    }

    if (env->a20_mask != -1) {
        return 0;
    }
    if (env->efer != 0 || env->msr_bndcfgs != 0) {
        return 0;
    }
    for (i = 0; i < 4; ++i) {
        if (env->bndcs_regs.cfgu || env->bndcs_regs.sts ||
            env->bnd_regs[i].lb || env->bnd_regs[i].ub) {
            return 0;
        }
    }

    if (!xlle_aot_seg_flat_code32(&env->segs[R_CS])) {
        return 0;
    }
    if (!xlle_aot_seg_flat_data32(&env->segs[R_DS]) ||
        !xlle_aot_seg_flat_data32(&env->segs[R_SS])) {
        return 0;
    }

    return 1;
}

static int xlle_aot_span_within_target_page(uint32_t linear, size_t size) {
    uint64_t end;
    if (size == 0 || size > TARGET_PAGE_SIZE) {
        return 0;
    }
    end = (uint64_t)linear + (uint64_t)size - 1u;
    if (end > UINT32_MAX) {
        return 0;
    }
    return ((linear ^ (uint32_t)end) & (uint32_t)TARGET_PAGE_MASK) == 0 ? 1 : 0;
}

void* xlle_unicorn_aot_ram_ptr(void* engine, uint32_t linear, size_t size,
                               unsigned access) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    CPUState* cpu;
    CPUX86State* env;
    int mmu_idx;
    CPUTLBEntry* entry;
    target_ulong tlb_addr;

    if (!uc || !uc->cpu || !uc->cpu->env_ptr) {
        return NULL;
    }
    if (!xlle_aot_span_within_target_page(linear, size)) {
        return NULL;
    }

    cpu = uc->cpu;
    env = (CPUX86State*)cpu->env_ptr;
    mmu_idx = cpu_mmu_index(env, access == XLLE_UNICORN_AOT_ACCESS_FETCH);
    if (mmu_idx < 0 || mmu_idx >= NB_MMU_MODES) {
        return NULL;
    }

    entry = tlb_entry(env, (uintptr_t)mmu_idx, (target_ulong)linear);
    switch (access) {
    case XLLE_UNICORN_AOT_ACCESS_READ:
        tlb_addr = entry->addr_read;
        break;
    case XLLE_UNICORN_AOT_ACCESS_WRITE:
        tlb_addr = entry->addr_write;
        break;
    case XLLE_UNICORN_AOT_ACCESS_FETCH:
        tlb_addr = entry->addr_code;
        break;
    default:
        return NULL;
    }

    if (!tlb_hit(uc, tlb_addr, (target_ulong)linear)) {
        unsigned victim;
        // A code page and its physical mirror can collide in the direct TLB.
        // Inspect the already-cached victim entries without promoting/refilling
        // them; QEMU invalidates these together with the direct table.
        for (victim = 0; victim < CPU_VTLB_SIZE; ++victim) {
            entry = &env_tlb(env)->d[mmu_idx].vtable[victim];
            tlb_addr = access == XLLE_UNICORN_AOT_ACCESS_READ ? entry->addr_read :
                       access == XLLE_UNICORN_AOT_ACCESS_WRITE ? entry->addr_write : entry->addr_code;
            if (tlb_hit(uc, tlb_addr, (target_ulong)linear)) break;
        }
        if (victim == CPU_VTLB_SIZE) return NULL;
    }
    // Native writes perform explicit physical code invalidation. NOTDIRTY is
    // software code tracking, not a missing architectural write permission;
    // the write TLB was populated only after the MMU's permission/A-D checks.
    // Snapshot and diagnostic observer contexts are excluded by the adapter.
    const target_ulong rejected = access == XLLE_UNICORN_AOT_ACCESS_WRITE
        ? TLB_FLAGS_MASK & ~TLB_NOTDIRTY : TLB_FLAGS_MASK;
    if ((tlb_addr & rejected) != 0) {
        return NULL;
    }
    if (entry->addend == 0) {
        return NULL;
    }

    return (void*)((uintptr_t)linear + entry->addend);
}

void xlle_unicorn_invalidate_ram(void* engine, uint32_t physical, size_t size) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    uint64_t at = physical;
    uint64_t end = at + size;
    if (!uc || !uc->cpu || end > (UINT64_C(1) << 32)) return;
    while (at < end) {
        MemoryRegion* mr = uc->memory_mapping(uc, at);
        uint64_t part;
        ram_addr_t start;
        if (!mr || mr->end <= at) return;
        part = end < mr->end ? end - at : mr->end - at;
        if (memory_region_is_ram(mr)) {
            start = memory_region_get_ram_addr(mr) + at - mr->addr;
            tb_invalidate_phys_range(uc, start, start + part);
        }
        at += part;
    }
}

int xlle_unicorn_share_ram_identity(void* engine, uint32_t original, uint32_t alias) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    MemoryRegion *a, *b;
    if (!uc) return 0;
    a = uc->memory_mapping(uc, original);
    b = uc->memory_mapping(uc, alias);
    if (!a || !b || a == b || a->addr != original || b->addr != alias ||
        !a->ram_block || !b->ram_block ||
        a->ram_block->host != b->ram_block->host ||
        a->ram_block->used_length != b->ram_block->used_length ||
        a->ram_block->max_length != b->ram_block->max_length) return 0;
    // Both blocks retain their own list nodes/destructors and PREALLOC host
    // ownership. Only their backing RAM identity is shared. Any host-pointer
    // reverse lookup now yields the same offset regardless of MRU/list order.
    b->ram_block->offset = a->ram_block->offset;
    tlb_flush(uc->cpu);
    return 1;
}

void xlle_unicorn_protect_code_aliases(void* engine, uint64_t first, uint64_t second) {
    struct uc_struct* uc = (struct uc_struct*)engine;
    // Called only when generating a TB, replacing the virtual-only scan with
    // the existing host-span scan. Include victim TLBs and every MMU mode.
    if (first != UINT64_MAX)
        tlb_reset_dirty(uc->cpu, (uintptr_t)qemu_map_ram_ptr(uc, NULL, first & TARGET_PAGE_MASK),
                        TARGET_PAGE_SIZE);
    if (second != UINT64_MAX)
        tlb_reset_dirty(uc->cpu, (uintptr_t)qemu_map_ram_ptr(uc, NULL, second), TARGET_PAGE_SIZE);
}

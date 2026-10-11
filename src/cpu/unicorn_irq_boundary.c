#include "unicorn_irq_boundary.h"
#include "uc_priv.h"
#include "cpu.h"

int xlle_unicorn_irq_inhibited(void *engine)
{
    struct uc_struct *uc=(struct uc_struct*)engine;
    if(!uc || uc->arch!=UC_ARCH_X86 || !uc->cpu || !uc->cpu->env_ptr)return 1;
    return (((CPUX86State*)uc->cpu->env_ptr)->hflags & HF_INHIBIT_IRQ_MASK)!=0;
}

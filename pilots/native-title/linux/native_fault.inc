/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native Linux faults terminate explicitly. Native-title builds do not use
 * signal recovery to emulate an unimplemented instruction or MMIO service.
 * Only async-signal-safe operations run after a fatal host signal.
 */
#include <signal.h>
#include <unistd.h>
static void native_fault_hex(char *out,uintptr_t value)
{
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<sizeof(uintptr_t)*2;++i)out[i]=hex[(value>>((sizeof(uintptr_t)*2-1-i)*4))&15];
}
static void native_fault_signal(int signal,siginfo_t *info,void *context)
{
    (void)context;
    char line[]="[PILOT STOP] native Linux signal=00 address=0x0000000000000000\n";
    line[33]=(char)('0'+signal/10);line[34]=(char)('0'+signal%10);
    native_fault_hex(line+46,(uintptr_t)info->si_addr);
    ssize_t ignored=write(STDERR_FILENO,line,sizeof(line)-1);(void)ignored;
    _exit(2);
}
static void install_fault_handler(void)
{
    struct sigaction action={0};action.sa_sigaction=native_fault_signal;action.sa_flags=SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    const int signals[]={SIGSEGV,SIGBUS,SIGILL,SIGFPE};
    for(unsigned i=0;i<sizeof(signals)/sizeof(signals[0]);++i)
        if(sigaction(signals[i],&action,NULL))stop("cannot install native Linux fault diagnostic",signals[i]);
}

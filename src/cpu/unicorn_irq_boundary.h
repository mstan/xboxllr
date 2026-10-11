#ifndef XLLE_UNICORN_IRQ_BOUNDARY_H
#define XLLE_UNICORN_IRQ_BOUNDARY_H
#ifdef __cplusplus
extern "C" {
#endif
/* Read the selected x86 engine's real interrupt-inhibition state, including
 * STI/MOV SS. Compile with that engine's own private headers; callers never
 * infer structure offsets or replace hidden state. Invalid engines inhibit. */
int xlle_unicorn_irq_inhibited(void *engine);
#ifdef __cplusplus
}
#endif
#endif

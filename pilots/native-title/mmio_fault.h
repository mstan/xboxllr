#ifndef PILOT_MMIO_FAULT_H
#define PILOT_MMIO_FAULT_H
#include <windows.h>
#include <stdint.h>
typedef const char *(*PilotMmioAccess)(uint32_t,int,uint32_t*);
typedef const char *(*PilotMmioAccessSized)(uint32_t,unsigned,int,uint32_t*);
/* Decode only actual x64 MOV dword loads/stores emitted by the C compiler.
 * Validate the effective address against the fault before touching hardware.
 * Unsupported host instructions return zero; the existing fault stop remains. */
int pilot_mmio_fault(EXCEPTION_POINTERS*,unsigned char*,PilotMmioAccess,const char**);
/* Explicit device aperture; byte MOV/MOVZX and dword MOV only. The callback
 * receives actual transaction width and rejects unsupported device accesses. */
int pilot_mmio_fault_range(EXCEPTION_POINTERS*,unsigned char*,uint32_t,uint32_t,
                          PilotMmioAccessSized,const char**);
#endif

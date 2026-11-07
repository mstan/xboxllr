/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "windows.h"
typedef struct {USHORT Length,MaximumLength;PWSTR Buffer;} UNICODE_STRING,*PUNICODE_STRING;
typedef struct {ULONG Length;HANDLE RootDirectory;PUNICODE_STRING ObjectName;ULONG Attributes;void *SecurityDescriptor,*SecurityQualityOfService;} OBJECT_ATTRIBUTES,*POBJECT_ATTRIBUTES;
typedef struct {union {NTSTATUS Status;void *Pointer;};uintptr_t Information;} IO_STATUS_BLOCK,*PIO_STATUS_BLOCK;
#define InitializeObjectAttributes(p,n,a,r,s) (*(p)=(OBJECT_ATTRIBUTES){sizeof(OBJECT_ATTRIBUTES),(r),(n),(a),(s),NULL})
#ifdef __cplusplus
extern "C" {
#endif
LONG pilot_posix_nt_create(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,void*,ULONG);
LONG pilot_posix_nt_open(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,ULONG,ULONG);
LONG pilot_posix_nt_read(HANDLE,HANDLE,void*,void*,PIO_STATUS_BLOCK,void*,ULONG,PLARGE_INTEGER,PULONG);
LONG pilot_posix_nt_write(HANDLE,HANDLE,void*,void*,PIO_STATUS_BLOCK,void*,ULONG,PLARGE_INTEGER,PULONG);
LONG pilot_posix_nt_query(HANDLE,PIO_STATUS_BLOCK,void*,ULONG,ULONG);
LONG pilot_posix_nt_set(HANDLE,PIO_STATUS_BLOCK,void*,ULONG,ULONG);
LONG pilot_posix_nt_volume(HANDLE,PIO_STATUS_BLOCK,void*,ULONG,ULONG);
LONG pilot_posix_nt_flush(HANDLE,PIO_STATUS_BLOCK);
LONG pilot_posix_nt_directory(HANDLE,HANDLE,void*,void*,PIO_STATUS_BLOCK,void*,ULONG,ULONG,BOOLEAN,PUNICODE_STRING,BOOLEAN);
LONG pilot_posix_nt_create_event(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,ULONG,BOOLEAN);
LONG pilot_posix_nt_set_event(HANDLE,PLONG);LONG pilot_posix_nt_reset_event(HANDLE,PLONG);
LONG pilot_posix_nt_query_event(HANDLE,ULONG,void*,ULONG,PULONG);
LONG pilot_posix_nt_wait(HANDLE,BOOLEAN,PLARGE_INTEGER);
LONG pilot_posix_nt_wait_many(ULONG,const HANDLE*,ULONG,BOOLEAN,PLARGE_INTEGER);
LONG pilot_posix_nt_signal_wait(HANDLE,HANDLE,BOOLEAN,PLARGE_INTEGER);
LONG pilot_posix_nt_delay(BOOLEAN,PLARGE_INTEGER);
LONG pilot_posix_nt_create_mutant(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,BOOLEAN);
LONG pilot_posix_nt_release_mutant(HANDLE,PLONG);
LONG pilot_posix_nt_query_mutant(HANDLE,ULONG,void*,ULONG,PULONG);
#ifdef __cplusplus
}
#endif

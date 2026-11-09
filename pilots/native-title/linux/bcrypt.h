/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "windows.h"
typedef void *BCRYPT_HASH_HANDLE;
typedef uintptr_t BCRYPT_ALG_HANDLE;
#define BCRYPT_SHA1_ALG_HANDLE ((BCRYPT_ALG_HANDLE)1)
#define BCRYPT_SHA256_ALG_HANDLE ((BCRYPT_ALG_HANDLE)2)
#ifdef __cplusplus
extern "C" {
#endif
LONG BCryptCreateHash(BCRYPT_ALG_HANDLE,BCRYPT_HASH_HANDLE*,unsigned char*,ULONG,unsigned char*,ULONG,ULONG);
LONG BCryptHashData(BCRYPT_HASH_HANDLE,unsigned char*,ULONG,ULONG);
LONG BCryptFinishHash(BCRYPT_HASH_HANDLE,unsigned char*,ULONG,ULONG);
LONG BCryptDestroyHash(BCRYPT_HASH_HANDLE);
LONG BCryptHash(BCRYPT_ALG_HANDLE,unsigned char*,ULONG,unsigned char*,ULONG,unsigned char*,ULONG);
#ifdef __cplusplus
}
#endif

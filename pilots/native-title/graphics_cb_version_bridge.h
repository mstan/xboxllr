#pragma once
/* Native writers retain their original payload and error contract. Other
 * supported donors keep their original buffer implementation. */
#ifdef PILOT_D3D_PHOBOS
#include "graphics_cb_version_owner.h"
#define PILOT_NATIVE_CB_BEFORE_WRITE(slot) pilot_native_cb_before_write(slot)
#define PILOT_NATIVE_CB_UNMAPPED(slot) pilot_native_cb_unmapped(slot)
#define PILOT_NATIVE_CB_FORGET(slot) pilot_native_cb_forget(slot)
#define PILOT_NATIVE_CB_CURRENT(buffer) pilot_native_cb_current(buffer)
#define PILOT_NATIVE_CB_MAP(context,slot,mode,mapped) pilot_native_cb_map(slot,context,mode,mapped)
#define PILOT_NATIVE_BUFFER_FULL_WRITE_OWNED(buffer) pilot_native_buffer_full_write_owned(buffer)
#define PILOT_NATIVE_BUFFER_BEFORE_RANGE_WRITE(buffer,context) pilot_native_buffer_before_range_write(buffer,context)
#else
#define PILOT_NATIVE_CB_BEFORE_WRITE(slot) ((void)0)
#define PILOT_NATIVE_CB_UNMAPPED(slot) ((void)0)
#define PILOT_NATIVE_CB_FORGET(slot) ((void)0)
#define PILOT_NATIVE_CB_CURRENT(buffer) (buffer)
#define PILOT_NATIVE_BUFFER_FULL_WRITE_OWNED(buffer) 0
#define PILOT_NATIVE_BUFFER_BEFORE_RANGE_WRITE(buffer,context) NULL
#define PILOT_NATIVE_CB_MAP(context,slot,mode,mapped) \
    ID3D11DeviceContext_Map(context,(ID3D11Resource*)*(slot),0,mode,0,mapped)
#endif
#define PILOT_NATIVE_CB_RELEASE(buffer) do { \
    PILOT_NATIVE_CB_FORGET(&(buffer));ID3D11Buffer_Release(buffer); \
}while(0)

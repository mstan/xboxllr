/* Authored R09 uploaded-input ownership; existing graphics owner lock. */
#pragma once
#include <d3d11.h>
enum { PILOT_CB_PRESSURE=0,PILOT_CB_UNSUPPORTED_MAP=1 };
#ifdef __cplusplus
extern "C" {
#endif
/* Set before borrowing. Callback submits/discards every CPU-retained HD job.
 * UNSUPPORTED_MAP additionally invalidates all collecting/in-flight output
 * aspects and degrades the frame: arbitrary partial maps are outside this
 * full-write prototype. Never wait for GPU completion or borrow recursively. */
int pilot_native_cb_version_barrier(void (*submit_or_discard)(unsigned reason));
/* Repair exactly required_bind_flags: CONSTANT_BUFFER, INDEX_BUFFER or VERTEX_BUFFER.
 * Return zero for an unknown class/context to use actual-state inspection.
 * INDEX preserves format/offset; VERTEX preserves every stride/offset. */
void pilot_native_cb_binding_owner(int (*rebind)(ID3D11DeviceContext *,UINT required_bind_flags,
    ID3D11Buffer *(*resolve)(ID3D11Buffer *)));
/* Teardown abandons logical ownership before in-flight packet pins retire;
 * those physical backings are released, never returned to a reuse pool. */
void pilot_native_cb_discard_owners(void);
/* Register CBs, DYNAMIC/CPU_WRITE vertex/index streams (<=32MiB), or
 * DEFAULT/CPU0/VERTEX-only buffers (<=32MiB), each with its native alignment.
 * Native allocations are not charged against the32MiB EXTRA-backing budget.
 * Stream append maps must use the checked range API below. No DEFAULT-index,
 * mixed-bind or arbitrary partial-map support. */
void pilot_native_cb_track(ID3D11Buffer **owner);
/* CB/DEFAULT-VB whole updates: call before the existing UpdateSubresource.
 * Dynamic WRITE_DISCARD callers use map() below, including its Map-failure
 * rollback. Untracked/unsupported buffers retain their original native path. */
void pilot_native_cb_before_write(ID3D11Buffer **owner);
HRESULT pilot_native_cb_map(ID3D11Buffer **owner,ID3D11DeviceContext *,D3D11_MAP,D3D11_MAPPED_SUBRESOURCE *);
/* A retained dynamic version permits only monotonic non-overlapping appends.
 * DISCARD publishes a new backing and starts a new written extent. Generic
 * DISCARD maps conservatively declare the entire backing written. */
HRESULT pilot_native_stream_map(ID3D11Buffer **owner,ID3D11DeviceContext *,D3D11_MAP,
    UINT offset,UINT bytes,D3D11_MAPPED_SUBRESOURCE *);
void pilot_native_cb_unmapped(ID3D11Buffer **owner);
/* Call before the logical owner's existing Release, while its slot is valid.
 * Borrowed versions survive owner destruction until their last event retires. */
void pilot_native_cb_forget(ID3D11Buffer **owner);
/* No capture copies. Pin the SAME uploaded CB/index/VB used by the native draw.
 * Existing captured COM reference stays owned by the job. Release the token
 * after its existing GPU event, or after cancellation before submission. */
int pilot_native_cb_version_borrow(ID3D11Buffer *,void **token);
void pilot_native_cb_version_release(void **token);
/* For native COM restores of a logical owned buffer: resolve before binding.
 * Borrowed result; callers retaining it must AddRef normally. */
ID3D11Buffer *pilot_native_cb_current(ID3D11Buffer *);
/* Any registered physical version, including blocked/retired/unsupported:
 * epoch capture must NOT use its legacy unpinned DEFAULT-VB admission. */
int pilot_native_buffer_registered(ID3D11Buffer *);
/* True only for a current, closed DEFAULT-VB owner. Caller must perform the
 * original prepared-RHI WHOLE update, whose hook rotates or pressure-flushes. */
int pilot_native_buffer_full_write_owned(ID3D11Buffer *current);
/* NULL: unhandled; caller keeps its old flush/fallback. Otherwise return a
 * borrowed current/new VB. If pinned, copy old->new once to preserve untouched
 * bytes BEFORE publishing. Caller then performs its original range update and
 * CPU shadow write exactly once. No dynamic VB, map or CPU payload expansion. */
ID3D11Buffer *pilot_native_buffer_before_range_write(ID3D11Buffer *current,ID3D11DeviceContext *);

#ifdef __cplusplus
}
#endif

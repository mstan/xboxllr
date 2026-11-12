/* Optional enhanced mirror of a COMPLETED original full native CopyResource.
 * The caller owns the graphics lock and retires its opaque snapshot token with
 * its scratch image. The completed-copy primitive does no allocation; the
 * exported snapshot bridge owns an optional pair/map. Neither touches canonical
 * copy results, resamples pixels, changes bindings, or handles partial copies.
 */
#ifdef PILOT_D3D_PHOBOS
static int pilot_native_copy_shapes(ID3D11Texture2D *a,ID3D11Texture2D *b)
{
    if(!a || !b)return 0;
    D3D11_TEXTURE2D_DESC x,y;ID3D11Texture2D_GetDesc(a,&x);ID3D11Texture2D_GetDesc(b,&y);
    /* Exact formats are deliberately conservative. A compatible typeless
     * reinterpretation may be admitted separately; never silently convert. */
    return x.Width==y.Width && x.Height==y.Height && x.Format==y.Format &&
        x.MipLevels==y.MipLevels && x.ArraySize==y.ArraySize &&
        x.SampleDesc.Count==y.SampleDesc.Count && x.SampleDesc.Quality==y.SampleDesc.Quality;
}
static int pilot_native_copy_completed(ID3D11Resource *destination,ID3D11Resource *source)
{
    pilot_native_epoch_flush_reason(COST_EPOCH_COPY);
    if(!destination || !source)return 0;
    struct PilotNativePlane *to=pilot_native_plane_find(destination);
    struct PilotNativePlane *from=pilot_native_plane_find(source);
    /* A missing destination registration cannot preserve an HD snapshot.
     * The caller must retain a distinct scratch map if the source is paired. */
    if(!to) {
        if(from && pilot_native_enabled())pilot_native_degrade();
        return 0;
    }
    if(to==from) {
        /* Logical aliases already share the same companion. Copying an image
         * into itself would be an invalid D3D11 operation, not extra fidelity. */
        if(!pilot_native_enabled() || !to->valid_aspects) {
            to->valid_aspects=to->coherent=0;pilot_native_degrade();return 0;
        }
        return 1;
    }
    /* The native destination has already changed. Every declined companion
     * copy must invalidate its prior history, even if it was coherent before. */
    to->valid_aspects=to->coherent=0;
    if(!pilot_native_enabled() || !from || !from->valid_aspects ||
       !to->image || !from->image || !to->enhanced || !from->enhanced ||
       to->enhanced==to->image || from->enhanced==from->image ||
       to->native_width!=from->native_width || to->native_height!=from->native_height ||
       to->width!=from->width || to->height!=from->height ||
       !pilot_native_copy_shapes(to->image,from->image) ||
       !pilot_native_copy_shapes(to->enhanced,from->enhanced)) {
        pilot_native_degrade();return 0;
    }
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    if(!context){pilot_native_degrade();return 0;}
    /* This is still a full-extent copy. It can preserve populated depth even
     * when stencil remains undefined; never promote a partial aspect mask. */
    if(to->enhanced!=from->enhanced)
        ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)to->enhanced,(ID3D11Resource*)from->enhanced);
    to->valid_aspects=from->valid_aspects;to->coherent=from->coherent;return 1;
}

struct PilotNativeSnapshotOwner {
    struct PilotNativePlane *plane;
    struct PilotNativeMap *map;
};
void pilot_native_snapshot_barrier(void)
{pilot_native_epoch_flush_reason(COST_EPOCH_COPY);}
void pilot_native_snapshot_release(void **token)
{
    if(!token || !*token)return;
    pilot_native_epoch_flush_reason(COST_EPOCH_COPY);
    struct PilotNativeSnapshotOwner *owner=*token;*token=NULL;
    pilot_native_map_release(owner->map);
    pilot_native_plane_release(owner->plane);
    free(owner);
}
void pilot_native_snapshot_copy(RhiImage *destination,RhiImage *source,void **token)
{
    pilot_native_epoch_flush_reason(COST_EPOCH_COPY);
    if(!token)return;
    if(!pilot_native_enabled()){pilot_native_snapshot_release(token);return;}
    ID3D11Resource *destination_resource=NULL,*source_resource=NULL;
    struct PilotNativePlane *from=NULL;
    ID3D11Texture2D *destination_image=destination?(ID3D11Texture2D*)rhi_d3d11_native_image(destination):NULL;
    ID3D11Texture2D *source_image=source?(ID3D11Texture2D*)rhi_d3d11_native_image(source):NULL;
    if(!destination_image || !source_image)goto failed;
    if(FAILED(ID3D11Texture2D_QueryInterface(destination_image,&IID_ID3D11Resource,(void**)&destination_resource)) ||
       FAILED(ID3D11Texture2D_QueryInterface(source_image,&IID_ID3D11Resource,(void**)&source_resource)))goto failed;
    from=pilot_native_plane_find(source_resource);
    if(!from || !from->enhanced || !from->width || !from->height){from=NULL;goto failed;}
    /* Retain the source across retirement of an old destination token, even
     * when aliases happen to share the prior snapshot owner. */
    pilot_native_plane_retain(from);
    struct PilotNativeSnapshotOwner *owner=*token;
    if(owner && (owner->plane->image!=destination_image || owner->plane->width!=from->width ||
                 owner->plane->height!=from->height)) {
        pilot_native_snapshot_release(token);owner=NULL;
    }
    if(!owner) {
        owner=calloc(1,sizeof(*owner));
        if(!owner)goto failed;
        owner->plane=pilot_native_plane_create(destination_image,from->width,from->height);
        owner->map=owner->plane?pilot_native_map_create(destination_image,owner->plane):NULL;
        if(!owner->map) {
            pilot_native_plane_release(owner->plane);free(owner);goto failed;
        }
        *token=owner;
    }
    /* Full-copy publication is independent of canonical success. Transfer
     * populated aspects without promoting an incomplete source to coherent. */
    (void)pilot_native_copy_completed(destination_resource,source_resource);
    goto finished;
failed:
    /* If a paired output cannot publish its feedback snapshot, a missing
     * destination map must not masquerade as an ordinary native-only texture
     * on the next HD draw. Keep that draw and presentation on native output. */
    if(from)from->valid_aspects=from->coherent=0;
    if(destination_resource) {
        struct PilotNativePlane *stale=pilot_native_plane_find(destination_resource);
        if(stale)stale->valid_aspects=stale->coherent=0;
    }
    if(*token) {
        struct PilotNativeSnapshotOwner *stale=*token;
        stale->plane->valid_aspects=stale->plane->coherent=0;
    }
    pilot_native_degrade();
finished:
    if(from)pilot_native_plane_release(from);
    if(destination_resource)ID3D11Resource_Release(destination_resource);
    if(source_resource)ID3D11Resource_Release(source_resource);
}
#else
static int pilot_native_copy_completed(ID3D11Resource *destination,ID3D11Resource *source)
{(void)destination;(void)source;return 0;}
#endif

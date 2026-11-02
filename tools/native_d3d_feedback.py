"""Expose the pinned donor's real feedback copy to the native draw adapter."""
import re

def prepare_feedback(code):
    def once(old,new):
        nonlocal code
        if code.count(old)!=1:raise ValueError('unreviewed donor feedback: '+old[:65])
        code=code.replace(old,new)
    once('static void up_ring_shutdown(void);',
         'static void up_ring_shutdown(void);\nstatic void pilot_feedback_shutdown(void);\n'
         'extern void pilot_native_snapshot_copy(RhiImage *,RhiImage *,void **);\n'
         'extern void pilot_native_snapshot_release(void **);\n'
         'extern void pilot_native_snapshot_barrier(void);\n'
         'extern void pilot_native_draw_prepared(unsigned,unsigned);\n'
         'static RhiImage *pilot_output_color, *pilot_output_depth;\n'
         'static int pilot_feedback_alias(RhiImage *a,RhiImage *b) {\n'
         '    return a && b && rhi_d3d11_native_image(a)==rhi_d3d11_native_image(b);\n}\n')
    once('        up_ring_shutdown();','        pilot_feedback_shutdown();\n        up_ring_shutdown();')
    once('        RhiView *srv = d3d8_base_srv(pTexture);',
         '        RhiImage *image=d3d8_base_resource(pTexture);\n'
         '        RhiView *srv=(pilot_feedback_alias(image,pilot_output_color) || pilot_feedback_alias(image,pilot_output_depth)) ? NULL : d3d8_base_srv(pTexture);')
    # Even NULL must unbind the previous stage view. Guest binding remains real.
    once('        if (srv)\n            rhi_set_textures(Stage, 1, &srv);',
         '        rhi_set_textures(Stage, 1, &srv);')
    once('static unsigned long g_feedback_copies;',
         'static unsigned long g_feedback_copies;\nstatic unsigned long pilot_feedback_allocations;')
    once('    return 1;\n}\n\n/* Bind the copy on every stage',
         '    ++pilot_feedback_allocations;\n    return 1;\n}\n\n/* Bind the copy on every stage')
    once('static DWORD feedback_begin(void)\n{\n    RhiImage *target = g_cur_rt ? g_cur_rt->image : NULL;',
         'static DWORD feedback_for_target(RhiImage *target)\n{')
    once('    if (!target || g_cur_rt->subresource != 0)\n        return 0;',
         '    if (!target) return 0;')
    once('!g_cur_textures[stage] || d3d8_base_resource(g_cur_textures[stage]) != target',
         '!g_cur_textures[stage] || !pilot_feedback_alias(d3d8_base_resource(g_cur_textures[stage]),target)')
    code,n=re.subn(r'            if \(!feedback_scratch\(target\) \|\| rhi_image_copy\(g_feedback.img, target\) != 0\) \{.*?\n            \}',
        '            pilot_native_snapshot_barrier();\n'
        '            if (!feedback_scratch(target) || rhi_image_copy(g_feedback.img, target) != 0)\n'
        '                pilot_d3d_backend_stop("native feedback scratch allocation/copy failed");\n'
        '            pilot_native_snapshot_copy(g_feedback.img,target,&g_feedback.native_plane_owner);',code,flags=re.S)
    if n!=1:raise ValueError('unreviewed donor feedback failure')
    once('static void feedback_end(DWORD mask)',
         'static DWORD feedback_begin(void) {\n'
         '    if(g_cur_rt && g_cur_rt->subresource)pilot_d3d_backend_stop("feedback mip is unqualified");\n'
         '    return feedback_for_target(pilot_output_color ? pilot_output_color : (g_cur_rt ? g_cur_rt->image : NULL));\n'
         '}\nstatic void feedback_end(DWORD mask)')
    once('else if (d3d8_base_resource(g_cur_textures[stage]) != target)',
         'else if (!pilot_feedback_alias(d3d8_base_resource(g_cur_textures[stage]),target) &&\n'
         '                 !pilot_feedback_alias(d3d8_base_resource(g_cur_textures[stage]),pilot_output_depth))')
    # Share the existing allocator while retaining a distinct destination slot.
    # Ordinary feedback may change size/format between corrected draws.
    once('''static struct {
    RhiImage     *img;
    RhiView      *srv;
    RhiImageDesc  desc;
} g_feedback;''', '''typedef struct PilotFeedbackScratch {
    RhiImage     *img;
    RhiView      *srv;
    RhiImageDesc  desc;
    void        *native_plane_owner;
} PilotFeedbackScratch;
static PilotFeedbackScratch g_feedback,pilot_blend_destination;
static unsigned long pilot_blend_copies,pilot_blend_allocations;''')
    start=code.index('static int feedback_scratch(RhiImage *target)')
    end=code.index('\n/* Bind the copy on every stage',start)
    allocator=code[start:end].replace('static int feedback_scratch(RhiImage *target)',
        'static int feedback_scratch_for(PilotFeedbackScratch *scratch,RhiImage *target,unsigned long *allocations)')
    allocator=allocator.replace('g_feedback.', 'scratch->').replace('++pilot_feedback_allocations;', '++(*allocations);')
    # Preserve existing conditional cleanup/control flow: a comma expression
    # retires the owned map immediately before EACH scratch image destruction.
    retire='rhi_image_destroy(scratch->img);'
    if retire not in allocator:raise ValueError('unreviewed feedback scratch retirement')
    allocator=allocator.replace(retire,
        '(pilot_native_snapshot_release(&scratch->native_plane_owner),rhi_image_destroy(scratch->img));')
    allocator+='''
static int feedback_scratch(RhiImage *target) {
    return feedback_scratch_for(&g_feedback,target,&pilot_feedback_allocations);
}
'''
    code=code[:start]+allocator+code[end:]
    # Intercept the donor's exact prepared UP submission while its ring/IA is
    # still retained and its original feedback scope is active. Never infer a
    # second draw's count/start after DrawPrimitiveUP has returned.
    draw='rhi_draw(vertex_count, 0);'
    if code.count(draw)!=1:raise ValueError('unreviewed prepared UP draw count')
    at=code.index(draw);up=code.rfind('dev_DrawPrimitiveUP(',0,at)
    if up<0 or code.rfind('feedback_begin(',up,at)<0 or code.find('feedback_end(',at)<0:
        raise ValueError('unreviewed prepared UP feedback scope')
    once(draw,'pilot_native_draw_prepared(vertex_count, 0);')
    code+='''
/* Native adapter access; resources remain owned by the donor/backend. */
RhiImage *pilot_donor_default_depth(void) { return g_device_state.depth_image; }
void pilot_donor_output_images(RhiImage *color,RhiImage *depth,int force) {
    if(!force && color==pilot_output_color && depth==pilot_output_depth)return;
    pilot_output_color=color;pilot_output_depth=depth;rebind_stage_srvs(color);
}
DWORD pilot_donor_feedback_begin(void) { return feedback_begin(); }
void pilot_donor_feedback_end(DWORD mask) { feedback_end(mask); }
void pilot_donor_feedback_stats(uint32_t *copies,uint32_t *allocations) {
    *copies=g_feedback_copies;*allocations=pilot_feedback_allocations;
}
/* Borrowed single-level snapshot. The adapter has proved current target shape,
 * state and single coverage before requesting it; never retain its source. */
RhiView *pilot_donor_blend_destination(void) {
    pilot_native_snapshot_barrier();
    RhiImage *target=pilot_output_color;
    if (!target || !pilot_feedback_alias(target,g_device_state.rhi_scene_image) ||
        !feedback_scratch_for(&pilot_blend_destination,target,&pilot_blend_allocations) ||
        rhi_image_copy(pilot_blend_destination.img,target)!=0)
        pilot_d3d_backend_stop("explicit blend destination allocation/copy failed");
    pilot_native_snapshot_copy(pilot_blend_destination.img,target,&pilot_blend_destination.native_plane_owner);
    ++pilot_blend_copies;
    return pilot_blend_destination.srv;
}
void pilot_donor_blend_destination_stats(uint32_t *copies,uint32_t *allocations) {
    *copies=pilot_blend_copies;*allocations=pilot_blend_allocations;
}
static void pilot_feedback_shutdown(void) {
    pilot_native_snapshot_release(&pilot_blend_destination.native_plane_owner);
    if(pilot_blend_destination.srv) {
        RhiView *none=NULL;rhi_set_textures(4,1,&none);
        rhi_view_destroy(pilot_blend_destination.srv);
    }
    if(pilot_blend_destination.img)rhi_image_destroy(pilot_blend_destination.img);
    memset(&pilot_blend_destination,0,sizeof(pilot_blend_destination));
    pilot_blend_copies=pilot_blend_allocations=0;
    pilot_native_snapshot_release(&g_feedback.native_plane_owner);
    if(g_feedback.srv)rhi_view_destroy(g_feedback.srv);
    if(g_feedback.img)rhi_image_destroy(g_feedback.img);
    memset(&g_feedback,0,sizeof(g_feedback));
    g_feedback_copies=pilot_feedback_allocations=0;
    pilot_output_color=pilot_output_depth=NULL;
}
'''
    return code


def prepare_cb_versions(code):
    """Bridge only the existing opaque RHI buffer owner; no renderer rewrite."""
    def once(old,new):
        nonlocal code
        if code.count(old)!=1:raise ValueError('unreviewed CB version boundary: '+old[:70])
        code=code.replace(old,new)
    once('/* ---- buffers ', '#include "graphics_cb_version_owner.inc"\n\n/* ---- buffers ')
    once('    if (v->v) ID3D11View_Release(v->v);',
         '    if (v->v) { prepared_state_forget_view(v->v); ID3D11View_Release(v->v); }')
    # Private swapchain views bypass d_view_destroy; release their metadata
    # ownership at the same physical-image boundary.
    once('        ID3D11View_Release(((struct RhiView *)g_present_view)->v);',
         '        prepared_state_forget_view(((struct RhiView *)g_present_view)->v);\n        ID3D11View_Release(((struct RhiView *)g_present_view)->v);')
    once('        ID3D11RenderTargetView_Release(g_present_rtv);',
         '        prepared_state_forget_view((ID3D11View*)g_present_rtv);\n        ID3D11RenderTargetView_Release(g_present_rtv);')
    once(
        '''static int d_swapchain_resize(uint32_t w, uint32_t h)
{
    ID3D11RenderTargetView *saved_rtv = NULL;
    ID3D11DepthStencilView *saved_dsv = NULL;
    int ok = -1;

    if (!g_swap)
        return -1;
    /* ResizeBuffers wants every reference to the old back buffer gone,
     * including any the context still holds. */
    ID3D11DeviceContext_OMGetRenderTargets(g_ctx, 1, &saved_rtv, &saved_dsv);
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 0, NULL, NULL);
    release_back_buffer_view();
    if (SUCCEEDED(IDXGISwapChain_ResizeBuffers(g_swap, 0, w, h, DXGI_FORMAT_UNKNOWN, g_swap_flags)))
        ok = make_back_buffer_view();
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &saved_rtv, saved_dsv);
    if (saved_rtv) ID3D11RenderTargetView_Release(saved_rtv);
    if (saved_dsv) ID3D11DepthStencilView_Release(saved_dsv);
    return ok;
}''',
        '''static int d_swapchain_resize(uint32_t w, uint32_t h)
{
    ID3D11RenderTargetView *saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {0};
    ID3D11DepthStencilView *saved_dsv = NULL;
    ID3D11Resource *backbuffer = NULL;
    unsigned replace_backbuffer = 0;
    int ok = -1;
    if (!g_swap) return -1;
    ID3D11DeviceContext_OMGetRenderTargets(g_ctx, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, &saved_dsv);
    if (g_present_rtv) ID3D11RenderTargetView_GetResource(g_present_rtv, &backbuffer);
    for (unsigned i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) if (saved[i]) {
        ID3D11Resource *resource = NULL;
        ID3D11RenderTargetView_GetResource(saved[i], &resource);
        int old_backbuffer = backbuffer && resource == backbuffer;
        if (resource) ID3D11Resource_Release(resource);
        if (old_backbuffer) {
            replace_backbuffer |= 1u << i;
            ID3D11RenderTargetView_Release(saved[i]); saved[i] = NULL;
        }
    }
    if (backbuffer) ID3D11Resource_Release(backbuffer);
    /* A private presentation view is an actual owner, too. Drop retained
     * binding/fact references before DXGI releases the underlying image.
     * The next snapshot repairs from the restored immediate context once. */
    prepared_state_invalidate(g_ctx);
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 0, NULL, NULL);
    release_back_buffer_view();
    HRESULT resized = IDXGISwapChain_ResizeBuffers(g_swap, 0, w, h, DXGI_FORMAT_UNKNOWN, g_swap_flags);
    /* A refused resize still owns its old buffer. Recreate its view so a
     * transient refusal does not permanently suppress future presentation. */
    int recreated = make_back_buffer_view();
    if (SUCCEEDED(resized) && recreated == 0) ok = 0;
    for (unsigned i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) if (replace_backbuffer & (1u << i)) {
        saved[i] = g_present_rtv;
        if (saved[i]) ID3D11RenderTargetView_AddRef(saved[i]);
    }
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, saved_dsv);
    for (unsigned i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        if (saved[i]) ID3D11RenderTargetView_Release(saved[i]);
    if (saved_dsv) ID3D11DepthStencilView_Release(saved_dsv);
    return ok;
}''')
    once('    b->desc = *d;\n    return b;',
         '    b->desc = *d;\n    pilot_native_cb_track(&b->b);\n    return b;')
    once('static void d_buffer_destroy(RhiBuffer *b)\n{',
         'static void d_buffer_destroy(RhiBuffer *b)\n{\n    pilot_native_cb_forget(&b->b);')
    once('ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)b->b, 0, (D3D11_MAP)mode, 0, &m)',
         'pilot_native_cb_map(&b->b,g_ctx,(D3D11_MAP)mode,&m)')
    once('    ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)b->b, 0);',
         '    ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)b->b, 0);\n    pilot_native_cb_unmapped(&b->b);')
    once('    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)b->b, 0, NULL, data, 0, 0);',
         '    pilot_native_cb_before_write(&b->b);\n    ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)b->b, 0, NULL, data, 0, 0);')
    # Uniform binds and native-buffer getters already resolve b->b on each call.
    # Reject preparer drift rather than introduce a second physical-pointer cache.
    if code.count('        nb[i] = b[i] ? b[i]->b : NULL;')!=1:
        raise ValueError('unreviewed CB uniform resolver')
    if code.count('void *rhi_d3d11_native_buffer(const RhiBuffer *b) { return b ? b->b : NULL; }')!=1:
        raise ValueError('unreviewed CB native resolver')
    return code

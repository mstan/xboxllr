"""Expose the pinned donor's real feedback copy to the native draw adapter."""
import re

def prepare_feedback(code):
    def once(old,new):
        nonlocal code
        if code.count(old)!=1:raise ValueError('unreviewed donor feedback: '+old[:65])
        code=code.replace(old,new)
    once('static void up_ring_shutdown(void);',
         'static void up_ring_shutdown(void);\nstatic void pilot_feedback_shutdown(void);\n'
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
        '            if (!feedback_scratch(target) || rhi_image_copy(g_feedback.img, target) != 0)\n'
        '                pilot_d3d_backend_stop("native feedback scratch allocation/copy failed");',code,flags=re.S)
    if n!=1:raise ValueError('unreviewed donor feedback failure')
    once('static void feedback_end(DWORD mask)',
         'static DWORD feedback_begin(void) {\n'
         '    if(g_cur_rt && g_cur_rt->subresource)pilot_d3d_backend_stop("feedback mip is unqualified");\n'
         '    return feedback_for_target(pilot_output_color ? pilot_output_color : (g_cur_rt ? g_cur_rt->image : NULL));\n'
         '}\nstatic void feedback_end(DWORD mask)')
    once('else if (d3d8_base_resource(g_cur_textures[stage]) != target)',
         'else if (!pilot_feedback_alias(d3d8_base_resource(g_cur_textures[stage]),target) &&\n'
         '                 !pilot_feedback_alias(d3d8_base_resource(g_cur_textures[stage]),pilot_output_depth))')
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
static void pilot_feedback_shutdown(void) {
    if(g_feedback.srv)rhi_view_destroy(g_feedback.srv);
    if(g_feedback.img)rhi_image_destroy(g_feedback.img);
    memset(&g_feedback,0,sizeof(g_feedback));
    g_feedback_copies=pilot_feedback_allocations=0;
    pilot_output_color=pilot_output_depth=NULL;
}
'''
    return code

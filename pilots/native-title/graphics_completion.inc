/* Binding references remain busy even after their last command completes.
 * Surface views share their parent's GPU work and fence cache, as in the
 * original Resource_IsBusy entry. Native serials are private implementation
 * state: guest fence words are cleared only after actual completion. */
static uint32_t graphics_resource_busy(uint32_t header)
{
    uint32_t canonical=header,handle;unsigned kind;
    if(header==graphics_headers || header==graphics_headers+32 || header==graphics_headers+64) {
        kind=PILOT_GFX_SURFACE;handle=graphics_surface_index(header);
    } else if(graphics_vertex_find(header)>=0) {
        kind=PILOT_GFX_VERTEX;handle=graphics_vertex(header)->native;
    } else {
        struct GraphicsTextureObject *texture=&graphics_textures[graphics_texture_slot(header)];
        if(texture->parent) {
            if(MEM32(header)&0x780000u)return 1;
            texture=&graphics_textures[texture->parent-1];canonical=texture->header;
        }
        kind=PILOT_GFX_TEXTURE;handle=texture->native;
    }
    if(MEM32(canonical)&0x780000u)return 1;
    if(pilot_gfx_resource_busy(kind,handle))return 1;
    MEM32(canonical+8)=0;return 0;
}

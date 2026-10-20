/* Original Release can tail-call DestroyResource while the last user count
 * is still one; other inline callers publish zero first. The object must have
 * no internal reference. Native-owned storage retires through its real owner,
 * never through the original heap using an unrelated native allocation. */
static void graphics_resource_destroy(uint32_t header)
{
    graphics_pointer(header,12);uint32_t common=MEM32(header),type=(common>>16)&7;
    if((common&0x78ffffu)>1)stop("destruction requires the last unbound resource reference",header);
    int slot=graphics_vertex_find(header);
    if(slot>=0) {
        struct GraphicsVertexObject *vertex=&graphics_vertices[slot];
        if(vertex->external)stop("original allocator resource destruction is unqualified",header);
        if(type || vertex->bindings || MEM32(header+4)!=(vertex->data&0x0fffffffu))
            stop("inconsistent owned vertex destruction",header);
        vertex->refs=0;graphics_vertex_collect(vertex);return;
    }
    slot=graphics_index_find(header);
    if(slot>=0) {
        struct GraphicsIndexObject *index=&graphics_indices[slot];
        if(index->borrowed)stop("original allocator resource destruction is unqualified",header);
        if(type!=1 || index->bindings || MEM32(header+4)!=index->data)
            stop("inconsistent owned index destruction",header);
        index->refs=0;graphics_index_collect(index);return;
    }
    for(unsigned i=0;i<graphics_texture_capacity;++i)if(graphics_textures[i].header==header) {
        struct GraphicsTextureObject *texture=&graphics_textures[i];
        if(texture->guest_owned)stop("original allocator resource destruction is unqualified",header);
        if(type!=(texture->parent?5u:4u) || texture->bindings || MEM32(header+4)!=texture->data)
            stop("inconsistent owned texture destruction",header);
        texture->refs=0;graphics_texture_collect(i);return;
    }
    fprintf(stderr,"[RESOURCE DESTROY] header=%08x common=%08x data=%08x lock=%08x\n",
        header,common,MEM32(header+4),MEM32(header+8));
    stop("original allocator resource destruction is unqualified",header);
}

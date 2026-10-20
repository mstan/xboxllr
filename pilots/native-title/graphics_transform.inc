/* Xbox matrix storage and viewport queries are observable even with a
 * programmable shader. Native float math and deferred GPU uploads remain
 * independent of the original driver's command packets and x87 history. */
#define TRANSFORM_FIELD(name) (PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_TRANSFORM_##name)
#define TRANSFORM_STATE(name) (PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_##name)
static float graphics_float(uint32_t address)
{float result;memcpy(&result,memory+address,4);return result;}
static void graphics_store_float(uint32_t address,float value)
{memcpy(memory+address,&value,4);}
static void graphics_viewport_offset_scale(float offset[4],float scale[4]);
static void graphics_projection_viewport(void)
{
    if(graphics_render_read(143)==2)stop("W-buffer projection is unqualified",2);
    uint32_t viewport=TRANSFORM_STATE(VIEWPORT);
    float x=MEM32(viewport+8)*graphics_float(TRANSFORM_FIELD(PIXEL_SCALE))*.5f;
    float y=MEM32(viewport+12)*graphics_float(TRANSFORM_FIELD(PIXEL_SCALE)+4)*-.5f;
    float low=graphics_float(viewport+16),high=graphics_float(viewport+20),depth=graphics_float(TRANSFORM_FIELD(DEPTH_SCALE));
    float screen[16]={x,0,0,0, 0,y,0,0, 0,0,(high-low)*depth,0, x,-y,low*depth,1};
    float result[16];pilot_gfx_matrix_multiply(result,(const float*)(memory+TRANSFORM_STATE(TRANSFORMS)+64),screen);
    memcpy(memory+TRANSFORM_FIELD(PROJECTION_VIEWPORT),result,sizeof(result));
}
static void graphics_transform_depth(const float *matrix)
{
    float near_depth=0,far_depth=1;
    if(matrix[10]!=matrix[11] && matrix[10]!=0) {
        near_depth=matrix[15]-matrix[14]/matrix[10]*matrix[11];
        far_depth=matrix[15]+(matrix[15]-matrix[14])/(matrix[10]-matrix[11])*matrix[11];
    }
    graphics_store_float(TRANSFORM_FIELD(PROJECTION_DEPTH),near_depth);
    graphics_store_float(TRANSFORM_FIELD(PROJECTION_DEPTH)+4,far_depth);
    graphics_store_float(TRANSFORM_FIELD(PROJECTION_DEPTH)+8,1/far_depth);
    uint32_t flags=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_RECORD_FLAGS;
    MEM32(flags)&=~2u;
    if(matrix[3]==0 && matrix[7]==0 && matrix[11]==0 && matrix[15]==1)MEM32(flags)|=2;
}
static void graphics_transform_viewport(const uint32_t viewport[6])
{
    memcpy(memory+TRANSFORM_STATE(VIEWPORT),viewport,24);graphics_projection_viewport();
    /* NV097 viewport writes also update reserved vertex constants 58/59.
     * Phobos's viewport bridge supplies the same missing handoff. Keep the
     * original hardware register values, including zero W and pixel bias;
     * these hardware writes do not change the SDK's user-constant cache. */
    if(!(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_RECORD_FLAGS)&0x200u)) {
        float values[8];graphics_viewport_offset_scale(values+4,values);
        pilot_gfx_shader_constants(58,2,values);
    }
}
static void graphics_transform_set(uint32_t state,uint32_t pointer)
{
    if(state>=10)stop("unqualified transform index",state);
#ifdef PILOT_TITLE_MODS
    if(title_mod && title_mod->transform)title_mod->transform(state,pointer,MEM32(g_esp));
#endif
    float matrix[16];memcpy(matrix,graphics_pointer(pointer,sizeof(matrix)),sizeof(matrix));
    if(pilot_gfx_transform(state,matrix)<0)stop("native transform update failed",state);
    memcpy(memory+TRANSFORM_STATE(TRANSFORMS)+state*64,matrix,sizeof(matrix));
    if(state==1){graphics_transform_depth(matrix);graphics_projection_viewport();}
    const char *capture=getenv("XLLE_PILOT_TRANSFORM_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;char path[1024];
        if(snprintf(path,sizeof(path),"%s/transform-%u.bin",capture,++sequence)>=(int)sizeof(path))stop("transform capture path too long",0);
        FILE *file=fopen(path,"wb");uint32_t header[]={0x54534c58,1,state,PILOT_D3D_SNAPSHOT_VIEWPORT+32};
        if(!file || fwrite(header,sizeof(header),1,file)!=1 || fwrite(matrix,sizeof(matrix),1,file)!=1 ||
            fwrite(memory+PILOT_D3D_DEVICE_OBJECT,header[3],1,file)!=1 || fclose(file))stop("transform capture failed",state);
    }
}
static void graphics_viewport_offset_scale(float offset[4],float scale[4])
{
    uint32_t viewport=TRANSFORM_STATE(VIEWPORT);
    float sx=graphics_float(TRANSFORM_FIELD(PIXEL_SCALE)),sy=graphics_float(TRANSFORM_FIELD(PIXEL_SCALE)+4);
    float depth=graphics_float(TRANSFORM_FIELD(DEPTH_SCALE)),low=graphics_float(viewport+16),high=graphics_float(viewport+20);
    scale[0]=MEM32(viewport+8)*sx*.5f;scale[1]=MEM32(viewport+12)*sy*-.5f;scale[2]=(high-low)*depth;scale[3]=0;
    float bias=(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_RECORD_FLAGS)&0x8000u) && graphics_render_read(152)?-.5f:0;
    offset[0]=MEM32(viewport)*sx+graphics_float(TRANSFORM_FIELD(SCREEN_OFFSET))+bias+scale[0];
    offset[1]=MEM32(viewport+4)*sy+graphics_float(TRANSFORM_FIELD(SCREEN_OFFSET)+4)+bias-scale[1];
    offset[2]=low*depth;offset[3]=0;
}
static void graphics_transform_init(void)
{
    /* Qualified presentation has no MSAA and a 24-bit integer depth surface. */
    graphics_store_float(TRANSFORM_FIELD(DEPTH_SCALE),16777215.0f);
    PilotPresentation presentation=graphics_presentation();float axis=(float)pilot_presentation_axis(&presentation);
    graphics_store_float(TRANSFORM_FIELD(PIXEL_SCALE),axis);graphics_store_float(TRANSFORM_FIELD(PIXEL_SCALE)+4,axis);
    MEM32(TRANSFORM_FIELD(SCREEN_OFFSET))=MEM32(TRANSFORM_FIELD(SCREEN_OFFSET)+4)=PILOT_D3D_SCREEN_BIAS;
    graphics_transform_depth((const float*)(memory+TRANSFORM_STATE(TRANSFORMS)+64));
    uint32_t viewport[6];if(pilot_gfx_get_viewport(viewport)<0)stop("missing initial viewport",0);
    graphics_transform_viewport(viewport);
}

static void test_graphics_transforms(void)
{
    uint32_t source=0x19c00,cache=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TRANSFORMS;
    float matrix[16]={0};for(unsigned i=0;i<4;++i)matrix[i*5]=1;
    for(unsigned state=0;state<10;++state) {
        matrix[12]=state*.125f;memcpy(memory+source,matrix,64);
        uint32_t args[]={state,source};call_graphics(PILOT_D3D_D3DDevice_SetTransform,args);
        CHECK(!memcmp(memory+cache+state*64,matrix,64));
    }
    uint32_t args[]={1,source};
    for(unsigned pass=0;pass<4;++pass) {
        memset(matrix,0,sizeof(matrix));matrix[0]=.75f;matrix[5]=1.5f;
        if(pass<3) {
            float near_depth=pass?3.25f:1,far_depth=pass==2?10000.0f:100;
            matrix[10]=far_depth/(far_depth-near_depth);matrix[11]=1;matrix[14]=-near_depth*matrix[10];
        } else {matrix[10]=1;matrix[15]=1;matrix[12]=2;}
        memcpy(memory+source,matrix,64);call_graphics(PILOT_D3D_D3DDevice_SetTransform,args);
        CHECK(!memcmp(memory+cache+64,matrix,64));
        CHECK(!!(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_RECORD_FLAGS)&2)==(pass==3));
        if(pass==3)CHECK(graphics_float(TRANSFORM_FIELD(PROJECTION_DEPTH))==1 && graphics_float(TRANSFORM_FIELD(PROJECTION_DEPTH)+4)==1);
    }
    uint32_t old_viewport[6],viewport[]={2,3,48,40,0x3e800000,0x3f400000};
    CHECK(pilot_gfx_get_viewport(old_viewport)==0);memcpy(memory+source,viewport,24);
    call_graphics(PILOT_D3D_D3DDevice_SetViewport,&source);CHECK(!memcmp(memory+TRANSFORM_STATE(VIEWPORT),viewport,24));
    uint32_t output[]={source+64,source+96};
    MEM32(source+60)=MEM32(source+80)=MEM32(source+92)=MEM32(source+112)=0xaabbccdd;
    call_graphics(PILOT_D3D_D3DDevice_GetViewportOffsetAndScale,output);
    float offset[4],scale[4];memcpy(offset,memory+output[0],16);memcpy(scale,memory+output[1],16);
    CHECK(offset[0]==26.53125f && offset[1]==23.53125f && offset[2]==4194303.75f && offset[3]==0);
    CHECK(scale[0]==24 && scale[1]==-20 && scale[2]==8388607.5f && scale[3]==0);
    float constants[192][4];pilot_gfx_shader_capture_constants(constants);
    CHECK(!memcmp(constants[58],scale,16) && !memcmp(constants[59],offset,16));
    CHECK(MEM32(source+60)==0xaabbccdd && MEM32(source+80)==0xaabbccdd && MEM32(source+92)==0xaabbccdd && MEM32(source+112)==0xaabbccdd);
    float expected[16];memcpy(expected,memory+TRANSFORM_FIELD(PROJECTION_VIEWPORT),64);
    memset(memory+TRANSFORM_FIELD(PROJECTION_VIEWPORT),0xcc,64);
    call_graphics(PILOT_D3D_D3D_UpdateProjectionViewportTransform,NULL);
    CHECK(!memcmp(expected,memory+TRANSFORM_FIELD(PROJECTION_VIEWPORT),64));
    memcpy(memory+source,old_viewport,24);call_graphics(PILOT_D3D_D3DDevice_SetViewport,&source);
    /* Restore ordinary identity transforms for the following shader fixtures. */
    memset(matrix,0,sizeof(matrix));for(unsigned i=0;i<4;++i)matrix[i*5]=1;
    for(unsigned state=0;state<10;++state){memcpy(memory+source,matrix,64);args[0]=state;call_graphics(PILOT_D3D_D3DDevice_SetTransform,args);}
    fprintf(stderr,"PASS: ten Xbox matrix slots, projection depth/affine cache, derived viewport matrix and offset/scale ABI\n");
}

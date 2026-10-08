static void graphics_test_shader_call(unsigned entry,const uint32_t *args)
{
    /* Offline original-x86 oracle inputs. Never enabled by title execution. */
    const char *capture=getenv("XLLE_PILOT_VERTEX_STATE_CAPTURE");FILE *file=NULL;
    uint32_t bytes=PILOT_D3D_VERTEX_PROGRAM_CACHE+136*16;
    if(capture && *capture) {
        static unsigned sequence;char path[1024];
        CHECK(snprintf(path,sizeof(path),"%s/state-%u.bin",capture,++sequence)<sizeof(path));
        file=fopen(path,"wb");CHECK(file);
        uint32_t header[]={0x56534c58,2,pilot_d3d_calls[entry].address,g_ecx,g_edx,bytes,
            args?args[0]:0,args && pilot_d3d_calls[entry].stack_bytes>=8?args[1]:0,
            args && pilot_d3d_calls[entry].stack_bytes>=12?args[2]:0,
            graphics_shaders[0].header,MEM32(graphics_shaders[0].header+8),0};
        CHECK(fwrite(header,sizeof(header),1,file)==1);
        CHECK(fwrite(memory+PILOT_D3D_DEVICE_OBJECT,bytes,1,file)==1);
        CHECK(fwrite(memory+0x19800,0x900,1,file)==1);
        CHECK(fwrite(&graphics_shaders[0],sizeof(graphics_shaders[0]),1,file)==1);
    }
    call_graphics(entry,args);
    if(file) {
        CHECK(fwrite(memory+PILOT_D3D_DEVICE_OBJECT,bytes,1,file)==1);
        CHECK(fwrite(memory+0x19800,0x900,1,file)==1);CHECK(fclose(file)==0);
    }
}
static void test_graphics_shader_binding(uint32_t handle,const struct PilotVertexProbeStream streams[16])
{
    uint32_t base=handle-1,cache=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS;
    uint32_t flags=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_RECORD_FLAGS;
    uint32_t original_flags=MEM32(flags);MEM32(flags)&=~PILOT_D3D_VERTEX_RECORD_DISABLE;
    graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShader,&handle);
    CHECK(MEM32(base)==1 && MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)==base);
    uint32_t output=0x198d0;MEM32(output-4)=MEM32(output+4)=0xaabbccdd;
    graphics_test_shader_call(PILOT_D3D_D3DDevice_GetVertexShader,&output);
    CHECK(MEM32(output)==handle && MEM32(output-4)==0xaabbccdd && MEM32(output+4)==0xaabbccdd);
    CHECK(!memcmp(memory+cache+100*16,graphics_shaders[0].constants[100],16));
    CHECK(!memcmp(memory+PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_PROGRAM_CACHE,graphics_shaders[0].program,32));
    float values[20];for(unsigned i=0;i<20;++i)values[i]=.125f*(i+1);
    memcpy(memory+0x19a00,values,sizeof(values));
    unsigned uploads=pilot_gfx_shader_uploads();
    g_ecx=0;g_edx=0x19a00;graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShaderConstant1,NULL);
    CHECK(!memcmp(memory+cache,values,16));
    g_ecx=188;g_edx=0x19a00;graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShaderConstant4,NULL);
    CHECK(!memcmp(memory+cache+188*16,values,64));
    uint32_t words=20;g_ecx=96;g_edx=0x19a00;
    graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShaderConstantNotInline,&words);
    CHECK(!memcmp(memory+cache+96*16,values,80));
    uint32_t query[]={0,0x19b00,5};graphics_test_shader_call(PILOT_D3D_D3DDevice_GetVertexShaderConstant,query);
    CHECK(!memcmp(memory+query[1],values,80));
    query[0]=(uint32_t)-96;query[2]=1;graphics_test_shader_call(PILOT_D3D_D3DDevice_GetVertexShaderConstant,query);
    CHECK(!memcmp(memory+query[1],values,16));
    CHECK(pilot_gfx_shader_uploads()==uploads); /* all writes still batched */
    float gpu[2][13][4];
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,NULL,&gpu[0][0][0])==0);
    CHECK(!memcmp(gpu[0][3],values+16,16) && pilot_gfx_shader_uploads()==uploads+1);
    words=4;g_ecx=100;g_edx=0x19a00;
    graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShaderConstantNotInlineFast,&words);
    CHECK(!memcmp(memory+cache+100*16,values+16,16)); /* fast skips CPU cache */
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,NULL,&gpu[0][0][0])==0);
    CHECK(!memcmp(gpu[1][3],values,16) && pilot_gfx_shader_uploads()==uploads+2);
    MEM32(flags)|=PILOT_D3D_VERTEX_RECORD_DISABLE;
    g_ecx=100;g_edx=0x19a10;graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShaderConstantNotInline,&words);
    CHECK(!memcmp(memory+cache+100*16,values+16,16));
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,NULL,&gpu[0][0][0])==0);
    CHECK(!memcmp(gpu[0][3],values+4,16));
    /* The fixed-size setter records even when the generic recorder is off. */
    g_ecx=100;g_edx=0x19a20;graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShaderConstant1,NULL);
    CHECK(!memcmp(memory+cache+100*16,values+8,16));
    graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShader,&handle);
    CHECK(!memcmp(memory+cache+100*16,values+8,16));
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,NULL,&gpu[0][0][0])==0);
    CHECK(!memcmp(gpu[0][3],graphics_shaders[0].constants[100],16));
    MEM32(flags)&=~PILOT_D3D_VERTEX_RECORD_DISABLE;
    graphics_test_shader_call(PILOT_D3D_D3DDevice_SetVertexShader,&handle);
    CHECK(!memcmp(memory+cache+100*16,graphics_shaders[0].constants[100],16));
    uploads=pilot_gfx_shader_uploads();
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,NULL,&gpu[0][0][0])==0);
    CHECK(pilot_gfx_shader_uploads()==uploads && MEM32(base)==1); /* unchanged data, no upload */
    uint32_t pixel=0;call_graphics(PILOT_D3D_D3DDevice_SetPixelShader,&pixel);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER)==0);
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,NULL,&gpu[0][0][0])==0);
    CHECK(!memcmp(gpu[0][3],graphics_shaders[0].constants[100],16)); /* pixel bind preserves VS */
    MEM32(flags)=original_flags;
    fprintf(stderr,"PASS: bound GPU shader, program/declaration cache, fastcall constants, signed queries, recorder policy and batched native uploads\n");
}
static void test_graphics_shaders(void)
{
    uint32_t declaration[]={0x12345,0x20000000,0x40420000,0x82000064,0x3f800000,0x40000000,0x40400000,0x40800000,0xffffffffu};
    /* MOV oPos,v0; MOV oD0,c[100], encoded against the NV2A ISA. */
    uint32_t program[]={0x00022078,0,0x0020001b,0x08000000,0x0000f800,0,0x002c801b,0x0c000000,0x0000f819};
    memcpy(memory+0x19800,declaration,sizeof(declaration));memcpy(memory+0x19840,program,sizeof(program));
    uint32_t args[]={0x19800,0x19840,0x198c4,0};
    MEM32(0x198c0)=MEM32(0x198c4)=MEM32(0x198c8)=0xcdcdcdcdu;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateVertexShader,args)==0);
    uint32_t handle=MEM32(0x198c4),base=handle&~1u;
    CHECK((handle&1) && base && MEM32(base)==1 && MEM32(base+4)==0x10 && MEM32(base+8)==2);
    CHECK(MEM32(0x198c0)==0xcdcdcdcdu && MEM32(0x198c8)==0xcdcdcdcdu);
    CHECK(MEM32(base+0x14)==0 && MEM32(base+0x18)==0 && MEM32(base+0x1c)==0x42);
    CHECK(graphics_shaders[0].header==base && graphics_shaders[0].constant_mask[3]==16);
    CHECK(!memcmp(graphics_shaders[0].constants[100],declaration+4,16));
    float vertices[2][4]={{.25f,-.5f,.75f,1},{-.125f,.25f,.5f,1}},constants[192][4]={0},outputs[2][13][4];
    struct PilotVertexProbeStream streams[16]={0};streams[0]=(struct PilotVertexProbeStream){vertices,16};
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,constants,&outputs[0][0][0])==0);
    CHECK(!memcmp(vertices[0],outputs[0][0],16) && !memcmp(vertices[1],outputs[1][0],16));
    test_graphics_shader_binding(handle,streams);
    uint64_t resident=vm_committed(0,0x100000000ull),service=vm_service_bytes;
    vm_service_bytes=64u*1024*1024;MEM32(0x198c4)=0xdeadbeefu;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateVertexShader,args)==0x8007000eu);
    vm_service_bytes=service;
    CHECK(MEM32(0x198c4)==0xdeadbeefu && graphics_shaders[0].header==base && !graphics_shaders[1].header);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: shader stdcall/HRESULT/odd guest handle, declaration constants, real GPU output and OOM output/ownership preservation\n");
    /* The outer fixture releases/recreates the device and checks that all
     * header allocations return to baseline, exercising shader destruction. */
}

static void test_graphics_pixels(void)
{
    uint32_t definition[60];
    for(unsigned i=0;i<60;++i)definition[i]=0x10203040u+i;
    memcpy(memory+0x19800,definition,sizeof(definition));
    MEM32(0x19900)=MEM32(0x19904)=MEM32(0x19908)=0xcdcdcdcdu;
    uint32_t args[]={0x19800,0x19904};
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreatePixelShader,args)==0);
    uint32_t handle=MEM32(0x19904);
    CHECK(handle && !(handle&3) && MEM32(handle)==1 && MEM32(handle+4)==1 && MEM32(handle+8)==handle+12);
    CHECK(MEM32(0x19900)==0xcdcdcdcdu && MEM32(0x19908)==0xcdcdcdcdu);
    memset(memory+0x19800,0xcc,sizeof(definition));
    CHECK(!memcmp(memory+MEM32(handle+8),definition,sizeof(definition)));
    uint64_t service=vm_service_bytes,resident=vm_committed(0,0x100000000ull);
    vm_service_bytes=64u*1024*1024;MEM32(0x19904)=0xdeadbeefu;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreatePixelShader,args)==0x8007000eu);
    vm_service_bytes=service;
    CHECK(MEM32(0x19904)==0xdeadbeefu && graphics_pixels[0]==handle && !graphics_pixels[1]);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: pixel definition stdcall/HRESULT, independent owned copy, guest descriptor and OOM output preservation\n");
#ifdef PILOT_D3D_PHOBOS
    memset(definition,0,sizeof(definition));definition[8]=1;definition[9]=0x1100;definition[43]=0xff204060;
    definition[57]=0x32103210;definition[58]=0x10101010;definition[59]=0x10;
    memcpy(memory+0x19800,definition,sizeof(definition));
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreatePixelShader,args)==0);
    uint32_t bound=MEM32(args[1]),query=0x19921;
    uint32_t objects[2]={MEM32(bound),MEM32(bound+4)};
    for(unsigned i=0;i<3;++i) {
        uint32_t select=i==1?0:bound;
        call_graphics(PILOT_D3D_D3DDevice_SetPixelShader,&select);
        memset(memory+query-4,0xcd,12);
        /* Independent original RET 4 contract: do not infer the expected
         * stack from the generated metadata that the dispatcher consumes. */
        g_esp=0x18000;MEM32(g_esp)=0x12340;MEM32(g_esp+4)=query;
        MEM32(g_esp+8)=0xcafebabe;
        pilot_d3d_dispatch(PILOT_D3D_D3DDevice_GetPixelShader);
        CHECK(g_esp==0x18008 && MEM32(g_esp)==0xcafebabe);
        CHECK(MEM32(query)==select && MEM32(query-4)==0xcdcdcdcdu && MEM32(query+4)==0xcdcdcdcdu);
        CHECK(MEM32(bound)==objects[0] && MEM32(bound+4)==objects[1]);
        CHECK(!memcmp(memory+bound+12,definition,sizeof(definition)));
    }
    float constants[2][4]={{.25f,.5f,.75f,1},{-.2f,1.2f,0,.5f}};
    memcpy(memory+0x19a00,constants,sizeof(constants));
    uint32_t set[]={0,0x19a00,2};call_graphics(PILOT_D3D_D3DDevice_SetPixelShaderConstant,set);
    uint32_t packed[]={0xff4080bf,0x8000ff00};
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_PIXEL_PACKED_CONSTANTS)==packed[0]);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_PIXEL_PACKED_CONSTANTS+4)==packed[1]);
    for(unsigned i=0;i<8;++i) {
        if((i%4)<2)CHECK(MEM32(PILOT_D3D_RENDER_STATES+(10+i)*4)==packed[i%4]);
        CHECK(MEM32(PILOT_D3D_RENDER_STATES+(18+i)*4)==packed[i%2]);
    }
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+43*4)==packed[0] && MEM32(PILOT_D3D_RENDER_STATES+44*4)==packed[1]);
    CHECK(!memcmp(graphics_pixel_eye_vector,constants[0],12));
    CHECK(!memcmp(memory+bound+12,definition,sizeof(definition)) && MEM32(bound)==objects[0]);
    uint32_t previous=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER),fvf=0x44;
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    graphics_render_state(143,0,1);graphics_render_state(147,0,1);graphics_render_state(60,0,1);
    graphics_render_state(59,0,1);graphics_render_state(67,0x01010101,1);
    struct {float x,y,z,w;uint32_t color;} v[]={{8,8,.5f,1,0xffffffff},{56,8,.5f,1,0xffffffff},{32,56,.5f,1,0xffffffff}};
    memcpy(memory+0x19c00,v,sizeof(v));uint32_t draw[]={5,3,0x19c00,sizeof(v[0])};
    CHECK(pilot_gfx_clear(1,0xff000000,1,0)==0);
    call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);graphics_test_pixel(64,128,191,1);
    MEM32(PILOT_D3D_RENDER_STATES+8*4)=2;MEM32(PILOT_D3D_RENDER_STATES+9*4)=0x1200;
    call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);graphics_test_pixel(0,255,0,0);
    /* A subsequent update must reach the same borrowed shader without rebinding. */
    float changed[]={1,.25f,0,.5f};memcpy(memory+0x19a00,changed,16);set[0]=1;set[2]=1;
    call_graphics(PILOT_D3D_D3DDevice_SetPixelShaderConstant,set);
    call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);graphics_test_pixel(255,64,0,1);
    CHECK(!memcmp(graphics_pixel_eye_vector,constants[0],12));
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&previous);
    uint32_t fixed=0;call_graphics(PILOT_D3D_D3DDevice_SetPixelShader,&fixed);
    fprintf(stderr,"PASS: mapped pixel constants, packed original-query cache, eye vector and changing real GPU pixels\n");
    fprintf(stderr,"PASS: pixel shader query follows real binding, zero/unbind/rebind, borrowed lifetime and four-byte output\n");
#endif
}

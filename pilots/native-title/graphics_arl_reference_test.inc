static int check_scalar_address_reference(void)
{
    /* ARL a0,v0.x; MOV oPos,c[a0+96]. Distinct constants expose a dropped
     * address write; paired issue and arithmetic remain the donor's operations. */
    const uint32_t code[]={0,0x01a00000,0x08000000,0,
                           0,0x002c001b,0x0c000000,0x0000f803};
    const float input[]={-96,-87,4,-95.5f,3.9f,0,-0.0005f,0.9995f};
    const int address[]={-96,-87,4,-96,3,0,0,1};
    struct PilotVertexElement elements[16]={0};
    elements[0]=(struct PilotVertexElement){0x12,0,0};
    struct PilotVertexProbeStream streams[16]={0};
    streams[0]=(struct PilotVertexProbeStream){input,sizeof(float)};
    float constants[192][4],gpu[8][13][4];uint32_t shader=0;
    for(unsigned i=0;i<192;++i)for(unsigned lane=0;lane<4;++lane)
        constants[i][lane]=(float)(i*4+lane+1);
    CHECK(pilot_gfx_shader_create(code,2,elements,&shader)==0);
    CHECK(pilot_gfx_shader_probe(shader,streams,8,constants,&gpu[0][0][0])==0);
    for(unsigned sample=0;sample<8;++sample) {
        Nv2aVshCPUFullExecutionState storage={0};
        Nv2aVshExecutionState state=nv2a_vsh_emu_initialize_full_execution_state(&storage);
        for(unsigned reg=0;reg<13;++reg)storage.output_regs[reg*4+3]=1;
        memcpy(storage.context_regs,constants,sizeof(constants));
        storage.input_regs[0]=input[sample];
        for(unsigned i=0;i<2;++i) {
            Nv2aVshStep step={0};CHECK(nv2a_vsh_parse_step(&step,code+i*4)==NV2AVPR_SUCCESS);
            nv2a_vsh_emu_apply(&state,&step);
        }
        CHECK(storage.address_reg[0]==address[sample]);
        for(unsigned lane=0;lane<4;++lane) {
            float expected=constants[96+address[sample]][lane];
            CHECK(storage.output_regs[lane]==expected);
            CHECK(gpu[sample][0][lane]==expected);
        }
    }
    pilot_gfx_shader_destroy(shader);
    fprintf(stderr,"PASS scalar ARL: 8 signed/fractional indices, distinct constants, CPU and GPU expected outputs\n");
    return 0;
}

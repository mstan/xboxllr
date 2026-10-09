/* Diagnostic only: execute the original XDK's captured pretransformed GPU
 * program with the permissive NV2A decoder. Title words stay in private input. */
static int check_fvf_reference(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);uint32_t header[2],program[136*4];float constants[192][4];
    CHECK(fread(header,sizeof(header),1,file)==1 && header[0] && header[0]<=136 && header[1]==192);
    CHECK(fread(program,header[0]*16,1,file)==1 && fread(constants,sizeof(constants),1,file)==1 && fgetc(file)==EOF);fclose(file);
    const float positions[][4]={{8,8,.375f,1},{8,56,.375f,.5f},{56,8,.375f,.25f},{56,56,.375f,2},{95.5f,440.5f,0,0}};
    for(unsigned sample=0;sample<5;++sample) {
        Nv2aVshCPUFullExecutionState storage={0};Nv2aVshExecutionState state=nv2a_vsh_emu_initialize_full_execution_state(&storage);
        for(unsigned reg=0;reg<13;++reg)storage.output_regs[reg*4+3]=1;
        for(unsigned i=0;i<16;++i)storage.input_regs[i*4+3]=1;
        memcpy(storage.input_regs,positions[sample],16);memcpy(storage.context_regs,constants,sizeof(constants));
        for(unsigned i=0;i<header[0];++i) {
            Nv2aVshStep step={0};CHECK(nv2a_vsh_parse_step(&step,program+i*4)==NV2AVPR_SUCCESS);nv2a_vsh_emu_apply(&state,&step);
        }
        fprintf(stderr,"FVF reference sample=%u pos=(%g,%g,%g,%g) fog=%g\n",sample,
            storage.output_regs[0],storage.output_regs[1],storage.output_regs[2],storage.output_regs[3],storage.output_regs[5*4]);
        float expected[]={positions[sample][0]+.53125f,positions[sample][1]+.53125f,
            positions[sample][2]*16777215.0f,positions[sample][3]?1/positions[sample][3]:INFINITY};
        for(unsigned lane=0;lane<4;++lane) {
            if(isinf(expected[lane]))CHECK(isinf(storage.output_regs[lane]) && !signbit(storage.output_regs[lane]));
            else CHECK(fabsf(storage.output_regs[lane]-expected[lane])<=2e-5f*fmaxf(1,fabsf(expected[lane])));
        }
    }
    fprintf(stderr,"PASS: original pretransformed screen/depth/finite reciprocal contract; original RHW-zero W is infinite, native finite limiting W validated by 2D GPU fixtures\n");
    return 0;
}

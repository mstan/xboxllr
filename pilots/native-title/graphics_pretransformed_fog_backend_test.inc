#ifdef PILOT_D3D_PHOBOS
static int test_pretransformed_fog(void)
{
    struct {float p[4];uint32_t diffuse,specular;float uv[2];} vertices[3]={
        {{8,8,.5f,.5f},0x99cc331a,0x40000000,{0,0}},
        {{56,8,.5f,.5f},0x99cc331a,0x40000000,{0,0}},
        {{8,56,.5f,.5f},0x99cc331a,0x40000000,{0,0}}};
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_state(D3DRS_RANGEFOGENABLE,0);pilot_gfx_set_state(D3DRS_FOGCOLOR,0x00336699);
    pilot_gfx_pixel_fixed();
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);
        pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,stage?D3DTOP_DISABLE:D3DTOP_SELECTARG1);
    }
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    for(unsigned variant=0;variant<10;++variant) {
        const unsigned modes[]={0,1,2,3,3,3,3,1,3,0};unsigned mode=modes[variant],immediate=variant>=8;
        float projection[16]={0};for(unsigned i=0;i<4;++i)projection[i*5]=1;
        if(variant!=4){projection[11]=1;projection[15]=0;}
        const float parameters[3]={0,variant==4?1.0f:4.0f,.25f};
        for(unsigned i=0;i<3;++i){uint32_t word;memcpy(&word,parameters+i,4);pilot_gfx_set_state(D3DRS_FOGSTART+i,word);}
        pilot_gfx_set_state(D3DRS_FOGENABLE,variant!=5);pilot_gfx_set_state(D3DRS_FOGTABLEMODE,mode);
        for(unsigned i=0;i<3;++i)vertices[i].p[3]=variant==6?0:variant==7?.5f/(1u<<i):.5f;
        CHECK(!pilot_gfx_fvf_placement(0x1c4,0,0,1,0));
        float expanded[3][16][4]={0};
        if(immediate) {
            CHECK(!pilot_gfx_immediate_fvf(0x1c4));
            for(unsigned i=0;i<3;++i) {
                memcpy(expanded[i][0],vertices[i].p,16);
                expanded[i][3][0]=.1056f;expanded[i][3][1]=.1994f;expanded[i][3][2]=.8012f;expanded[i][3][3]=.61f;
                expanded[i][4][3]=.37f;
            }
        }
        if(variant!=5)CHECK(!pilot_gfx_fvf_fog(0x1c4,projection,immediate));
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        CHECK(!pilot_gfx_draw_memory(5,3,immediate?(const void*)expanded:(const void*)vertices,immediate?sizeof(expanded[0]):sizeof(vertices[0])));
        unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        /* Independent screen barycentrics at sample(16.5,16.5), with unequal
         * clip W: fog factors are evaluated at vertices before perspective
         * interpolation. Evaluating exp after interpolation differs. */
        const double b=8.5/48,a=1-2*b;
        const double varying=(a*.5*exp(-.5)+b*.25*exp(-1)+b*.125*exp(-2))/(a*.5+b*.25+b*.125);
        const double factors[]={64.0/255,exp(-.5),exp(-.25),.5,.5,1,1,varying,.5,.37};
        const double diffuse[]={immediate?.8012:204.0/255,immediate?.1994:51.0/255,immediate?.1056:26.0/255},fog[]={.2,.4,.6};
        const unsigned char *pixel=pixels+(16*64+16)*4;
        for(unsigned lane=0;lane<4;++lane) {
            int expected=lane==3?(immediate?156:153):(int)lround(255*(diffuse[lane]*factors[variant]+fog[lane]*(1-factors[variant])));
            if(abs((int)pixel[lane]-expected)>1)fprintf(stderr,"SCREEN FOG variant=%u lane=%u actual=%u expected=%d\n",variant,lane,pixel[lane],expected);
            CHECK(abs((int)pixel[lane]-expected)<=1);
        }
    }
    fprintf(stderr,"PASS pretransformed fog: supplied alpha, exp/exp2/linear W, affine Z, fog-disabled transition, zero-RHW distance, nonlinear factor interpolation, immediate float colors and preserved diffuse alpha\n");
    return 0;
}
#endif

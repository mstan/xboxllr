/* Independently authored geometry matches the bounded software reference's
 * counts. Real GPU work only; no substitute query results or timing guesses. */
static int visibility_quad(float left,float top,float right,float bottom,float z)
{
    struct {float x,y,z,w;uint32_t color;} v[]={
        {left,top,z,1,0xffffffff},{right,top,z,1,0xffffffff},{left,bottom,z,1,0xffffffff},
        {right,top,z,1,0xffffffff},{right,bottom,z,1,0xffffffff},{left,bottom,z,1,0xffffffff}};
    return pilot_gfx_draw_up(D3DPT_TRIANGLELIST,2,v,sizeof(v[0]),0x44);
}
static int visibility_expect(uint32_t index,uint32_t expected,uint64_t *stamp)
{
    uint32_t count=0xcccccccc;uint64_t time=0xeeeeeeeeeeeeeeeeull;
    DWORD start=GetTickCount();int32_t hr;
    do {
        hr=pilot_gfx_visibility_result(index,&count,&time);
        if((uint32_t)hr!=0x88760828u)break;
        CHECK(count==0xcccccccc && time==0xeeeeeeeeeeeeeeeeull);Sleep(1);
    } while(GetTickCount()-start<2000);
    if(hr || count!=expected)fprintf(stderr,"visibility %u: hr=%08x count=%u expected=%u\n",index,(unsigned)hr,count,expected);
    CHECK(hr==0 && count==expected);
    uint32_t repeated=0;uint64_t again=0;
    CHECK(pilot_gfx_visibility_result(index,&repeated,&again)==0 && repeated==count && again==time);
    if(stamp)*stamp=time;return 0;
}
static int test_visibility(void)
{
    pilot_gfx_pixel_fixed();
    uint32_t vp[]={0,0,64,64,0,0x3f800000};CHECK(pilot_gfx_viewport(vp)==0);
    for(unsigned stage=0;stage<4;++stage){
        pilot_gfx_texture_bind(stage,0);
        pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
        pilot_gfx_set_texture_state(stage,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    }
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_STENCILENABLE,0);pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);
    pilot_gfx_set_state(D3DRS_ZENABLE,1);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);
    pilot_gfx_set_state(D3DRS_ZFUNC,D3DCMP_LESS);
    CHECK(pilot_gfx_clear(3,0xff000000,1,0)==0);
    uint32_t count=123;uint64_t stamp=456,first=0,last=0;
    CHECK((uint32_t)pilot_gfx_visibility_result(0xffffffff,&count,&stamp)==0x88760828u && count==123 && stamp==456);
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(0,0,20,20,.2f)==0);
    CHECK(pilot_gfx_visibility_end(0)==0 && visibility_expect(0,400,&first)==0);
    CHECK(pilot_gfx_visibility_end(257)==0 && visibility_expect(257,400,&last)==0 && last>=first);
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(0,0,20,20,.3f)==0);
    CHECK(pilot_gfx_visibility_end(1)==0 && visibility_expect(1,0,NULL)==0);
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(10,0,30,20,.3f)==0);
    CHECK(pilot_gfx_visibility_end(2)==0 && visibility_expect(2,200,NULL)==0);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);
    pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,0);
    unsigned char before[64*64*4],after[64*64*4];CHECK(pilot_gfx_readback(before,sizeof(before))==0);
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(0,0,20,20,.2f)==0 && visibility_quad(0,0,20,20,.2f)==0);
    CHECK(pilot_gfx_visibility_end(3)==0 && visibility_expect(3,800,NULL)==0);
    CHECK(pilot_gfx_readback(after,sizeof(after))==0 && !memcmp(before,after,sizeof(before)));
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,1);pilot_gfx_set_state(D3DRS_ALPHAFUNC,D3DCMP_NEVER);
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(0,0,20,20,.2f)==0);
    CHECK(pilot_gfx_visibility_end(4)==0 && visibility_expect(4,0,NULL)==0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(54,0,74,20,.2f)==0);
    CHECK(pilot_gfx_visibility_end(5)==0 && visibility_expect(5,200,NULL)==0);
    /* Reset during an enabled interval discards only the previous measurement. */
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(0,0,20,20,.2f)==0);
    CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(0,0,10,20,.2f)==0);
    CHECK(pilot_gfx_visibility_end(0)==0 && visibility_expect(0,200,NULL)==0);
    CHECK(visibility_expect(257,400,NULL)==0);
    for(unsigned i=0;i<70;++i){
        CHECK(pilot_gfx_visibility_begin()==0 && visibility_quad(0,0,20,20,.2f)==0);
        CHECK(pilot_gfx_visibility_end(0)==0);
    }
    CHECK(visibility_expect(0,400,NULL)==0);
    pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);
    /* Teardown must also release a still-active real query. */
    CHECK(pilot_gfx_visibility_begin()==0);
    fprintf(stderr,"PASS: native visibility 400/0/200/800/0/200, masks, reset, index reuse, pending outputs, real GPU timestamps and repeated reads\n");
    return 0;
}

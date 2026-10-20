/* Original Xbox FLOATn beta declaration, independently enumerated offsets.
 * This does not claim a fixed-function blend matrix consumer. */
static int test_fvf_float_beta_layout(void)
{
    struct PilotVertexElement elements[16],plain[16];
    CHECK(pilot_gfx_fvf_float_beta_layout(0x1118,elements)==40);
    CHECK(elements[0].type==0x32 && elements[0].offset==0);
    CHECK(elements[1].type==0x22 && elements[1].offset==12);
    CHECK(elements[2].type==0x32 && elements[2].offset==20);
    CHECK(elements[9].type==0x22 && elements[9].offset==32);
    CHECK(pilot_gfx_fvf_float_beta_layout(0x118,plain)==40 && !memcmp(elements,plain,sizeof(plain)));
    for(unsigned n=1;n<=4;++n) {
        uint32_t fvf=(4+2*n)|0x1000|0x10|0x40|0x80|0x200|(3<<16)|(2<<18);
        CHECK(pilot_gfx_fvf_float_beta_layout(fvf,elements)==52+4*n);
        CHECK(elements[1].type==((n<<4)|2) && elements[1].offset==12);
        CHECK(elements[2].type==0x32 && elements[2].offset==12+4*n);
        CHECK(elements[3].type==0x40 && elements[3].offset==24+4*n);
        CHECK(elements[4].type==0x40 && elements[4].offset==28+4*n);
        CHECK(elements[9].type==0x12 && elements[9].offset==32+4*n);
        CHECK(elements[10].type==0x42 && elements[10].offset==36+4*n);
        for(unsigned i=0;i<16;++i) {
            CHECK(!elements[i].stream);
            if(i>=5 && i!=9 && i!=10)CHECK(!elements[i].type && !elements[i].offset);
        }
    }
    CHECK(pilot_gfx_fvf_float_beta_layout(0x1c4,elements)==pilot_gfx_fvf_layout(0x1c4,plain));
    CHECK(!memcmp(elements,plain,sizeof(plain)));
    CHECK(pilot_gfx_fvf_prepare_fixed(0x1118)<0);
    fprintf(stderr,"PASS original FLOAT1..4 beta declaration offsets, ignored1000, normals/colors/coordinate widths and no false fixed blend admission\n");
    return 0;
}

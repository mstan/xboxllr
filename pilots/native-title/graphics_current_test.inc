static int check_current_probe(uint32_t shader,const struct PilotVertexProbeStream streams[16],const float expected[4],const float (*positions)[4])
{
    float gpu[2][13][4];CHECK(!pilot_gfx_shader_probe(shader,streams,2,NULL,&gpu[0][0][0]));
    for(unsigned sample=0;sample<2;++sample)for(unsigned lane=0;lane<4;++lane) {
        CHECK(fabsf(gpu[sample][9][lane]-expected[lane])<1e-6f);
        if(positions)CHECK(gpu[sample][0][lane]==positions[sample][lane]);
    }
    return 0;
}
static int check_current_vertex_inputs(void)
{
    for(unsigned life=0;life<2;++life) {
        /* Documented native D3D initialization, tested independently of
         * packet updates; this does not establish cold silicon state. */
        float value[4];CHECK(pilot_gfx_vertex_current_get(7,value));
        CHECK(value[0]==0 && value[1]==0 && value[2]==0 && value[3]==1);
        CHECK(pilot_gfx_vertex_current_get(3,value));for(unsigned i=0;i<4;++i)CHECK(value[i]==1);
        const uint32_t code[]={0,0x00200e1b,0x08000000,0x0000f849};
        struct PilotVertexElement elements[16]={0};uint32_t shader=0;
        CHECK(!pilot_gfx_shader_create(code,1,elements,&shader) && !pilot_gfx_shader_bind(shader));
        uint32_t extents[16];CHECK(!pilot_gfx_shader_streams(extents));
        struct PilotVertexProbeStream streams[16]={0};float expected[]={.25f,-.5f,.75f,2};
        CHECK(pilot_gfx_vertex_current_set(7,expected) && !check_current_probe(shader,streams,expected,NULL));
        uint32_t word=0x3fc00000; /* X=1.5, leaving Y/Z/W unchanged. */
        CHECK(pilot_gfx_command_method(0x1a70,word,0) && pilot_gfx_vertex_current_get(7,value) && !memcmp(value,expected,16));
        CHECK(pilot_gfx_command_method(0x1a70,word,1));expected[0]=1.5f;
        CHECK(!check_current_probe(shader,streams,expected,NULL));
        CHECK(pilot_gfx_command_method(0x18bc,0xbe000000,1)); /* Y=-.125; 2F supplies Z=0,W=1. */
        expected[1]=-.125f;expected[2]=0;expected[3]=1;CHECK(!check_current_probe(shader,streams,expected,NULL));
        CHECK(pilot_gfx_command_method(0x195c,0x80402010,1));
        expected[0]=16/255.0f;expected[1]=32/255.0f;expected[2]=64/255.0f;expected[3]=128/255.0f;
        CHECK(!check_current_probe(shader,streams,expected,NULL));
        CHECK(!pilot_gfx_command_method(0x1a00,0,1) && !pilot_gfx_command_method(0x1880,0,1) && !pilot_gfx_command_method(0x1940,0,1));
        CHECK(!pilot_gfx_command_method(0x1a71,0,1) && !pilot_gfx_command_method(0x1b00,0,1));
        CHECK(pilot_gfx_vertex_current_get(7,value) && !memcmp(value,expected,16) && !pilot_gfx_vertex_current_set(16,value));
        pilot_gfx_shader_destroy(shader);
        const uint32_t mixed[]={0,0x0020001b,0x08000000,0x0000f800,0,0x00200e1b,0x08000000,0x0000f849};
        elements[0]=(struct PilotVertexElement){0x42,0,0};CHECK(!pilot_gfx_shader_create(mixed,2,elements,&shader) && !pilot_gfx_shader_bind(shader));
        CHECK(pilot_gfx_shader_streams(extents)==1 && extents[0]==16);
        const float positions[2][4]={{.125f,.25f,.5f,1},{.75f,.5f,.25f,1}};
        streams[0]=(struct PilotVertexProbeStream){positions,16};CHECK(!check_current_probe(shader,streams,expected,positions));
        pilot_gfx_shader_destroy(shader);
        CHECK(!check_program(mixed,2,elements,"stream-and-current-input-LLE-reference"));
        if(!life) {
            pilot_gfx_close();uint32_t p[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};CHECK(!pilot_gfx_create(p));
        }
    }
    const float defaults[]={0,0,0,1};CHECK(pilot_gfx_vertex_current_set(7,defaults));
    puts("PASS current vertex inputs: real bound GPU cbuffer updates after shader creation/binding, 4F/2F/4UB producers and probe no-mutation, mixed streams versus LLE, position/invalid guards and recreated native defaults");return 0;
}

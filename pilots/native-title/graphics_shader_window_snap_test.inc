/* Authored signed grid boundaries and W, independently expected before SO.
 * Exercise the production raster bytecode, preserving raw and texture output. */
static int check_shader_window_snap(void)
{
    const float xy[][2]={{.03125f,.09375f},{-.03125f,-.09375f},
        {.0625f,-.0625f},{.5f,-.5f},{63.53125f,64.03125f},
        {-63.53125f,-64.03125f},{640.03125f,480.03125f},{0,0}};
    const float w[]={.5f,1,2,4};
    enum {COUNT=32};
    struct {float position[4],texcoord[4];} vertices[COUNT];
    for(unsigned i=0;i<COUNT;++i) {
        vertices[i].position[0]=xy[i/4][0];vertices[i].position[1]=xy[i/4][1];
        vertices[i].position[2]=0;vertices[i].position[3]=w[i%4];
        const float texture[]={-.7f,3.5f,17.25f,2};
        memcpy(vertices[i].texcoord,texture,sizeof(texture));
    }
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        const uint32_t code[]={0,0x0020001b,0x08000000,0x0000f800,
            0,0x0020021b,0x08000000,0x0000f849};
        struct PilotVertexElement elements[16]={0};
        elements[0]=(struct PilotVertexElement){0x42,0,0};
        elements[1]=(struct PilotVertexElement){0x42,0,16};
        uint32_t shader=0;CHECK(!pilot_gfx_shader_create(code,2,elements,&shader));
        CHECK(!pilot_gfx_shader_bind(shader));
        float constants[192][4]={0};pilot_gfx_shader_constants(0,192,constants);
        const float scale[]={.03125f,-.03125f,1.0f/16777215,1},offset[]={-1,1,0,0};
        CHECK(!pilot_gfx_shader_raster(scale,offset));
        struct PilotVertexProbeStream streams[16]={0};
        streams[0]=(struct PilotVertexProbeStream){vertices,sizeof(vertices[0])};
        float raw[COUNT][52],raster[COUNT][52];
        CHECK(!pilot_gfx_shader_probe(shader,streams,COUNT,constants,&raw[0][0]));
        CHECK(!pilot_gfx_shader_raster_probe(shader,streams,COUNT,NULL,&raster[0][0]));
        for(unsigned i=0;i<COUNT;++i) {
            for(unsigned lane=0;lane<4;++lane) {
                CHECK(raw[i][lane]==vertices[i].position[lane]);
                CHECK(raw[i][9*4+lane]==vertices[i].texcoord[lane]);
                CHECK(raster[i][9*4+lane]==vertices[i].texcoord[lane]);
            }
            float x=truncf(vertices[i].position[0]*16)/16;
            float y=truncf(vertices[i].position[1]*16)/16;
            float expected[]={ (x*.03125f-1)*vertices[i].position[3],
                (y*-.03125f+1)*vertices[i].position[3],0,vertices[i].position[3]};
            for(unsigned lane=0;lane<4;++lane)CHECK(raster[i][lane]==expected[lane]);
        }
        fprintf(stderr,"PASS programmable window snap: lifetime=%u, %u signed grid/W cases; exact positions, raw and texcoords\n",lifetime,COUNT);
        pilot_gfx_shader_destroy(shader);pilot_gfx_close();
        if(lifetime<2){uint32_t pp[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};CHECK(!pilot_gfx_create(pp));}
    }
    return 0;
}

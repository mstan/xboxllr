#ifdef PILOT_D3D_PHOBOS
static void test_light_word(unsigned char *memory,unsigned address,uint32_t value)
{memcpy(memory+address,&value,4);}
static int test_lighting_cache(void)
{
    unsigned char memory[8192]={0};const unsigned device=128,table=1024;
    const struct PilotLightingLayout layout={0,4,8,32,144,104,140};
    const unsigned order[]={20,4,17,2,3,1,21,0,22,23};
    struct PilotLightingSnapshot snapshot={0};
    CHECK(pilot_lighting_cache(memory,sizeof(memory),device,&layout,&snapshot)==1 && !snapshot.count);
    test_light_word(memory,device,table);test_light_word(memory,device+4,32);
    test_light_word(memory,device+8,table+order[0]*144);
    for(unsigned i=0;i<10;++i) {
        struct PilotLightingLight light={0};light.type=3;light.diffuse[0]=(float)order[i];
        memcpy(memory+table+order[i]*144,&light,sizeof(light));
        test_light_word(memory,table+order[i]*144+104,7);
        test_light_word(memory,table+order[i]*144+140,i==9?0:table+order[i+1]*144);
    }
    CHECK(pilot_lighting_cache(memory,sizeof(memory),device,&layout,&snapshot)==1 && snapshot.count==8);
    for(unsigned i=0;i<8;++i)CHECK(snapshot.lights[i].diffuse[0]==order[i]);
    struct PilotLightingSnapshot before=snapshot;
    test_light_word(memory,table+order[1]*144+140,table+order[0]*144);
    CHECK(pilot_lighting_cache(memory,sizeof(memory),device,&layout,&snapshot)==PILOT_LIGHTING_CACHE_CYCLE);
    CHECK(!memcmp(&snapshot,&before,sizeof(snapshot)));
    test_light_word(memory,device+8,table+1);
    CHECK(pilot_lighting_cache(memory,sizeof(memory),device,&layout,&snapshot)==PILOT_LIGHTING_CACHE_NODE);
    test_light_word(memory,device+8,table+order[0]*144);
    test_light_word(memory,table+order[0]*144+104,4);
    CHECK(pilot_lighting_cache(memory,sizeof(memory),device,&layout,&snapshot)==PILOT_LIGHTING_CACHE_UNDEFINED);
    test_light_word(memory,device+4,UINT32_MAX);
    CHECK(pilot_lighting_cache(memory,sizeof(memory),device,&layout,&snapshot)==PILOT_LIGHTING_CACHE_RANGE);
    test_light_word(memory,device+4,0);test_light_word(memory,device+8,0);
    CHECK(pilot_lighting_cache(memory,sizeof(memory),device,&layout,&snapshot)==1 && !snapshot.count);
    for(unsigned i=0;i<8;++i)CHECK(!snapshot.lights[i].type);
    return 0;
}
static int test_lighting_pixel(unsigned fvf,unsigned vertex,const void *vertices,unsigned stride,
    const struct PilotLightingSnapshot *snapshot,const float expected[3])
{
    unsigned char storage[120]={0};CHECK(stride*3<=sizeof(storage));
    memcpy(storage,vertices,stride*3);
    pilot_gfx_vertex_upload(vertex,storage,sizeof(storage));pilot_gfx_vertex_bind(0,vertex,stride);
    CHECK(!pilot_gfx_lighting_publish(snapshot));CHECK(pilot_gfx_lighting_ready());
    CHECK(pilot_gfx_initializer_consumer(2,5,fvf));
    CHECK(!(fvf==0x1118?pilot_gfx_fvf_prepare_unblended(fvf):pilot_gfx_fvf_prepare_fixed(fvf)));
    CHECK(!pilot_gfx_clear(1,0xff0000ff,1,0) && !pilot_gfx_draw_vertices(5,0,3));
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    const unsigned char *pixel=pixels+(32*64+32)*4;
    for(unsigned i=0;i<3;++i) {
        int byte=(int)lroundf(fminf(1,fmaxf(0,expected[i]))*255);
        if(abs((int)pixel[i]-byte)>1) {
            fprintf(stderr,"LIGHTING PIXEL channel=%u actual=%u expected=%d fvf=%x lights=%u normalize=%u\n",
                i,pixel[i],byte,fvf,snapshot->count,snapshot->normalize);return 1;
        }
    }
    return 0;
}
static int test_fixed_lighting(void)
{
    CHECK(!test_lighting_cache());CHECK(!pilot_gfx_lighting_ready());
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,1);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);CHECK(pilot_gfx_xbox_render_state(137,0));
    CHECK(!pilot_gfx_initializer_consumer(2,5,0x1118));
    float identity[16]={0};for(unsigned i=0;i<4;++i)identity[i*5]=1;
    CHECK(!pilot_gfx_transform(0,identity) && !pilot_gfx_transform(1,identity) && !pilot_gfx_transform(6,identity));
    CHECK(pilot_gfx_xbox_texture_state(0,12,2) && pilot_gfx_xbox_texture_state(0,14,0));
    CHECK(pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,0));
    CHECK(pilot_gfx_xbox_texture_state(1,12,1));
    struct {float position[3],weights[2],normal[3],uv[2];} vertices[3]={
        {{-.8f,.8f,.5f},{0,0},{0,0,-2},{0,0}},
        {{.8f,.8f,.5f},{0,0},{0,0,-2},{0,0}},
        {{0,-.8f,.5f},{0,0},{0,0,-2},{0,0}}};
    uint32_t vertex=0;CHECK(!pilot_gfx_vertex_create(sizeof(vertices),&vertex));
    struct PilotLightingSnapshot s={0};memcpy(s.view,identity,64);
    s.material.diffuse[3]=1;s.material.ambient[0]=.5f;s.material.ambient[1]=.25f;
    s.material.emissive[2]=.125f;s.ambient=0xff808080;
    float expected[]={64.0f/255,32.0f/255,.125f};
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.count=1;s.lights[0].type=3;s.lights[0].direction[2]=1;
    s.lights[0].diffuse[0]=.25f;s.lights[0].diffuse[1]=.5f;
    s.material.diffuse[0]=.5f;s.material.diffuse[1]=.25f;s.ambient=0;
    expected[0]=expected[1]=.25f;expected[2]=.125f;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    CHECK(pilot_gfx_xbox_render_state(142,1));
    struct PilotInitializerState normal_state;CHECK(pilot_gfx_initializer_snapshot(&normal_state) && normal_state.normalize==1);
    s.normalize=normal_state.normalize;expected[0]=expected[1]=.125f;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    struct PilotLightingSnapshot finite=s;
    s.material.power=s.material.specular[0]=s.material.ambient[3]=NAN;
    s.lights[0].position[0]=s.lights[0].range=s.lights[0].falloff=NAN;
    s.lights[0].attenuation[0]=s.lights[0].theta=s.lights[0].phi=NAN;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s=finite;
    /* Missing COLOR1/COLOR2 must select uniform material, even with COLORVERTEX. */
    s.color_vertex=1;for(unsigned i=0;i<4;++i)s.sources[i]=i&1?2:1;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.color_vertex=0;s.lights[0].type=1;s.lights[0].position[2]=-100000;
    s.lights[0].range=200000;s.lights[0].attenuation[0]=2;
    expected[0]=expected[1]=.0625f;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.lights[0].range=10;expected[0]=expected[1]=0;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.lights[0].range=200000;s.lights[0].type=2;s.lights[0].theta=1;s.lights[0].phi=2;s.lights[0].falloff=1;
    expected[0]=expected[1]=.0625f;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.lights[0].direction[2]=-1;expected[0]=expected[1]=0;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.count=0;s.color_vertex=1;s.sources[3]=1;s.material.emissive[0]=.75f;
    struct {float p[3],n[3];uint32_t diffuse,specular;} colored[3];
    for(unsigned i=0;i<3;++i){memcpy(colored[i].p,vertices[i].position,12);memcpy(colored[i].n,vertices[i].normal,12);colored[i].diffuse=0xff204080;colored[i].specular=0xff804020;}
    expected[0]=32.0f/255;expected[1]=64.0f/255;expected[2]=128.0f/255;
    CHECK(!test_lighting_pixel(0xd2,vertex,colored,sizeof(colored[0]),&s,expected));
    s.sources[3]=2;expected[0]=128.0f/255;expected[2]=32.0f/255;
    CHECK(!test_lighting_pixel(0xd2,vertex,colored,sizeof(colored[0]),&s,expected));
    s.color_vertex=0;expected[0]=.75f;expected[1]=0;expected[2]=.125f;
    CHECK(!test_lighting_pixel(0xd2,vertex,colored,sizeof(colored[0]),&s,expected));
    /* View rotation must transform the light and normal together. */
    memset(&s,0,sizeof(s));memcpy(s.view,identity,64);
    s.view[0]=s.view[5]=0;s.view[1]=1;s.view[4]=-1;
    CHECK(!pilot_gfx_transform(0,s.view));s.normalize=1;s.material.diffuse[0]=.5f;
    s.material.diffuse[3]=1;s.count=1;s.lights[0].type=3;s.lights[0].diffuse[0]=.5f;
    s.lights[0].direction[0]=-1;
    for(unsigned i=0;i<3;++i){vertices[i].normal[0]=1;vertices[i].normal[2]=0;}
    expected[0]=.25f;expected[1]=expected[2]=0;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    CHECK(!pilot_gfx_transform(0,identity));memcpy(s.view,identity,64);
    for(unsigned i=0;i<3;++i){vertices[i].normal[0]=0;vertices[i].normal[2]=-2;}
    s.material.diffuse[0]=0;s.material.specular[0]=.5f;s.material.power=8;
    s.lights[0].diffuse[0]=0;s.lights[0].specular[0]=1;
    s.lights[0].direction[0]=0;s.lights[0].direction[2]=1;s.specular_enable=1;
    pilot_gfx_set_state(D3DRS_SPECULARENABLE,1);expected[0]=.5f;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.local_viewer=1;double sum=0;
    const double barycentric[]={.2353515625,.2548828125,.509765625};
    for(unsigned i=0;i<3;++i) {
        double x=vertices[i].position[0],y=vertices[i].position[1],z=.5;
        double distance=sqrt(x*x+y*y+z*z),hz=-1-z/distance;
        double hx=-x/distance,hy=-y/distance;
        double cosine=-hz/sqrt(hx*hx+hy*hy+hz*hz);
        sum+=barycentric[i]*pow(cosine,8);
    }
    expected[0]=(float)(.5*sum);
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    s.specular_enable=0;pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    CHECK(pilot_gfx_xbox_texture_state(0,14,4));expected[0]=0;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    CHECK(pilot_gfx_xbox_texture_state(0,14,0));
    /* XYZ+UV omits the normal array: lighting must read retained v2 rather
     * than disabling light or fabricating a normal. Test actual pixels across
     * cached preparations and a packet setter, plus explicit-array priority. */
    struct {float p[3],uv[2];} no_normal[3]={0};
    for(unsigned i=0;i<3;++i)memcpy(no_normal[i].p,vertices[i].position,12);
    float saved_normal[4],current_normal[4]={0,0,-.5f,1};
    CHECK(pilot_gfx_vertex_current_get(2,saved_normal) && pilot_gfx_vertex_current_set(2,current_normal));
    memset(&s,0,sizeof(s));memcpy(s.view,identity,64);
    s.material.diffuse[0]=.5f;s.material.diffuse[1]=.25f;s.material.diffuse[2]=.125f;s.material.diffuse[3]=1;
    s.count=1;s.lights[0].type=3;s.lights[0].direction[2]=1;
    for(unsigned i=0;i<3;++i)s.lights[0].diffuse[i]=1;
    expected[0]=.25f;expected[1]=.125f;expected[2]=.0625f;
    CHECK(!test_lighting_pixel(0x102,vertex,no_normal,sizeof(no_normal[0]),&s,expected));
    float opposite=1;uint32_t word;memcpy(&word,&opposite,4);
    CHECK(pilot_gfx_command_method(0x1a28,word,1));
    expected[0]=expected[1]=expected[2]=0;
    CHECK(!test_lighting_pixel(0x102,vertex,no_normal,sizeof(no_normal[0]),&s,expected));
    s.normalize=1;expected[0]=.5f;expected[1]=.25f;expected[2]=.125f;
    CHECK(!test_lighting_pixel(0x1118,vertex,vertices,sizeof(vertices[0]),&s,expected));
    current_normal[2]=-2;CHECK(pilot_gfx_vertex_current_set(2,current_normal));
    CHECK(!test_lighting_pixel(0x102,vertex,no_normal,sizeof(no_normal[0]),&s,expected));
    CHECK(pilot_gfx_vertex_current_set(2,saved_normal));
    struct PilotLightingSnapshot invalid=s;invalid.count=9;
    CHECK(pilot_gfx_lighting_publish(&invalid)<0 && pilot_gfx_lighting_ready());
    pilot_gfx_lighting_invalidate();CHECK(!pilot_gfx_lighting_ready());
    CHECK(!pilot_gfx_initializer_consumer(2,5,0x1118));
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);
    fprintf(stderr,"PASS native lighting: original list order/high indices/cache guards, inert-beta normal offset, ambient/emissive, directional normal control/view rotation, point attenuation/range, spot cone, material sources, local/infinite viewer/specular enable and stale-state invalidation\n");
    return 0;
}
#endif

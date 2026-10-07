// Independent PFIFO + software-raster visibility reference fixtures.
// NV097 wire facts: nxdk_pgraph_tests ZPass and original XDK command emission.
// No donor implementation bodies; all pixels come from the existing CPU raster.
static void test_visibility_reference() {
    std::string fault;
    std::vector<uint8_t> ram(0x40000,0);
    xlle::Nv2a gpu([&](const std::string& why){fault=why;});
    bool completion_ordered=true;
    gpu.set_mem_access(
        [&](uint32_t a,void* p,size_t n){if(a>ram.size()||n>ram.size()-a)return false;std::memcpy(p,ram.data()+a,n);return true;},
        [&](uint32_t a,const void* p,size_t n){
            if(a>ram.size()||n>ram.size()-a)return false;
            if(a>=0x30000 && a<0x30100 && (a&15)==12 && n==4)
                completion_ordered &= get32(ram,(a&~15u))!=0xcccccccc;
            std::memcpy(ram.data()+a,p,n);return true;
        });
    auto mmio=[&](uint32_t r,uint32_t v){gpu.mmio_write(xlle::Nv2a::kBase+r,4,v);};
    mmio(0x700008,1);mmio(0x70000c,0x80010020);mmio(0x700200,0x97);
    mmio(0x700010,2);mmio(0x700014,0x80010030);
    mmio(0x700300,0x3d);mmio(0x700304,0x3ffff);mmio(0x700308,0);
    mmio(0x700180,0x3d);mmio(0x700184,0xffff);mmio(0x700188,0x2000);
    mmio(0x002504,1);mmio(0x003204,0x100);mmio(0x00322c,0x18);
    mmio(0x003244,0);mmio(0x003220,1);mmio(0x003250,1);mmio(0x003200,1);
    uint32_t cursor=0;
    auto emit=[&](uint32_t m,uint32_t v){put32(ram,0x2000+cursor,(1u<<18)|m);put32(ram,0x2004+cursor,v);cursor+=8;};
    auto kick=[&](){mmio(0x800040,cursor);CHECK(fault.empty());};
    auto bits=[](float f){uint32_t n;std::memcpy(&n,&f,4);return n;};
    emit(0,1);emit(0x0194,2);emit(0x0198,2);emit(0x01a8,2);
    for(unsigned i=0;i<16;++i)emit(0x0680+i*4,(i%5)==0?0x3f800000:0);
    emit(0x0294,0x00020001);emit(0x1e94,0);
    emit(0x0200,64u<<16);emit(0x0204,64u<<16);emit(0x0208,0x128);
    emit(0x020c,0x01000100);emit(0x0210,0x10000);emit(0x0214,0x20000);
    emit(0x0290,0x00100001);emit(0x02c0,63u<<16);emit(0x02e0,63u<<16);
    emit(0x033c,0x207);emit(0x0350,0x8006);emit(0x0354,0x201);
    emit(0x0358,0x01010101);emit(0x035c,1);emit(0x030c,1);
    emit(0x037c,0x1d01);emit(0x038c,0x1b02);emit(0x0390,0x1b02);
    emit(0x0394,0);emit(0x0398,0x4b7fffff);emit(0x039c,0x405);emit(0x03a0,0x900);emit(0x1d78,1);
    emit(0x1e78,0x00210000);emit(0x1e60,1);
    emit(0x0ac0,0x04200000);emit(0x0260,0x14200000);
    emit(0x1e40,0xc00);emit(0x0aa0,0xc00);emit(0x0288,0xc);emit(0x028c,0x1c80);
    for(unsigned p=0;p<64*64;++p)put32(ram,0x20000+p*4,0xffffffff);
    kick();gpu.set_frame(120);gpu.set_frame_phase(500);
    auto quad=[&](float left,float top,float right,float bottom,float z){
        emit(0x17fc,5);
        for(auto xy:{std::pair{left,top},std::pair{right,top},std::pair{left,bottom},
                     std::pair{right,top},std::pair{right,bottom},std::pair{left,bottom}}){
            emit(0x1518,bits(xy.first));emit(0x151c,bits(xy.second));emit(0x1520,bits(z));
            emit(0x194c,0xffffffff);emit(0x1524,0x3f800000);
        }
        emit(0x17fc,0);kick();
    };
    auto report=[&](unsigned index,uint32_t expected){
        uint32_t a=0x30000+index*16;for(unsigned p=0;p<4;++p)put32(ram,a+p*4,0xcccccccc);
        put32(ram,a+12,0xffffffff);emit(0x17d0,0x01000000|a);
        CHECK(get32(ram,a+12)==0xffffffff);kick();
        CHECK(get32(ram,a+8)==expected);CHECK(get32(ram,a+12)==0);CHECK(completion_ordered);
        uint64_t stamp=uint64_t(get32(ram,a))|(uint64_t(get32(ram,a+4))<<32);
        CHECK(stamp==2008333333ull);
    };
    emit(0x17c8,1);emit(0x17cc,1);
    quad(0,0,20,20,50);report(0,400);
    quad(0,0,20,20,60);report(1,400); // completely hidden
    quad(10,0,30,20,60);report(2,600); // half hidden
    emit(0x17cc,0);quad(40,0,60,20,50);report(3,600);
    emit(0x17cc,1);quad(0,30,20,50,50);report(4,1000); // enable preserves count
    emit(0x17c8,1);report(5,0); // clear independent of enable
    emit(0x030c,0);emit(0x035c,0);emit(0x0358,0); // no depth/color writes
    const auto color_before=std::vector<uint8_t>(ram.begin()+0x10000,ram.begin()+0x14000);
    quad(0,0,20,20,50);quad(0,0,20,20,50);report(6,800); // count samples, not unique pixels
    CHECK(std::equal(color_before.begin(),color_before.end(),ram.begin()+0x10000));
    emit(0x17c8,1);emit(0x0300,1);emit(0x033c,0x200);quad(0,0,20,20,50);report(7,0); // alpha NEVER
    emit(0x0300,0);quad(54,0,74,20,50);report(8,200); // framebuffer clipping
    emit(0x17cc,0);emit(0x17c8,1);report(9,0);
    // Insufficient DMA report range must leave the completion sentinel intact.
    mmio(0x700304,0x30007);put32(ram,0x3000c,0xffffffff);emit(0x17d0,0x01030000);mmio(0x800040,cursor);
    CHECK(!fault.empty() && get32(ram,0x3000c)==0xffffffff);
    std::printf("PASS: LLE visibility counts 400/400/600/1000/0/800/0/200; masks, cumulative reports, ordered completion and DMA range\n");
}

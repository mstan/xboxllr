// Authored hardware-control cases. nxdk supplies the independent RAM-bank
// offsets and original driver sequence; no successful context transfer is
// inferred from a register write or accepted before its implementation.
static void test_pgraph_setup() {
    std::string failure;
    xlle::Nv2a gpu([&](const std::string& why){failure=why;});
    auto read=[&](uint32_t reg){return gpu.mmio_read(0xfd000000u+reg,4);};
    auto write=[&](uint32_t reg,uint32_t value){gpu.mmio_write(0xfd000000u+reg,4,value);};
    const uint32_t setup[][2]={
        {0x400080,0},{0x400084,0x118700},{0x400880,0x28c3ff},{0x40008c,0xf3de0479},
        {0x400090,0},{0x400094,4},{0x400b80,0x45ead10e},{0x400b84,0},
        {0x400098,0x78},{0x40009c,0x40},{0x400b88,0}};
    for(auto& r:setup){write(r[0],r[1]);CHECK(read(r[0])==r[1]);}
    CHECK(read(0x400700)==0);
    write(0x400084,0x1118700);CHECK(read(0x400084)==0x118700);
    for(uint32_t tile=0;tile<8;++tile) {
        write(0x100244+tile*16,0x1000+tile*0x1000);
        write(0x100248+tile*16,0x200+tile*0x100);
        for(uint32_t word=0;word<3;++word) {
            uint32_t value=read(0x100240+tile*16+word*4);
            write(0x400900+tile*16+word*4,value);
            CHECK(read(0x400900+tile*16+word*4)==value);
            uint32_t address=0xea0010+tile*4+word*0x20;
            write(0x400750,address);write(0x400754,value);
            CHECK(read(0x400750)==address+4);
            write(0x400750,address);CHECK(read(0x400754)==value);
        }
        CHECK(read(0x100300+tile*4)==0);
        write(0x400980+tile*4,0);write(0x400750,0xea0090+tile*4);write(0x400754,0);
    }
    write(0x4009a0,read(0x100324));write(0x4009a4,read(0x100200));write(0x4009a8,read(0x100204));
    CHECK(read(0x4009a4)==0x03070003 && !read(0x4009a0) && !read(0x4009a8));
    write(0x400750,0xea0000);write(0x400754,0x03070003);write(0x400754,0);
    write(0x400750,0xea0000);CHECK(read(0x400754)==0x03070003 && !read(0x400754));
    for(uint32_t r=0x40014c;r<=0x400158;r+=4){write(r,0);CHECK(read(r)==0);}
    write(0x400144,0x10000100);write(0x400148,0x03000000);write(0x400764,0x08000000);
    CHECK(read(0x400144)==0x10000100 && read(0x400148)==0x03000000);
    write(0x400080,0x70000);CHECK(read(0x400080)==0x70000);write(0x400080,0);
    write(0x7110a0,0x12345678);write(0x200,0xffffefff);
    CHECK(read(0x400084)==0 && read(0x400144)==0 && read(0x400750)==0);
    CHECK(read(0x400904)==0 && read(0x100244)==0x1000 && read(0x7110a0)==0x12345678);
    write(0x200,0xffffffff);
    CHECK(failure.empty());
    write(0x100240,2);CHECK(!failure.empty() && read(0x100240)==0);failure.clear();
    write(0x400750,0xea0010);write(0x400754,2);
    CHECK(!failure.empty() && read(0x400750)==0xea0010);failure.clear();
    write(0x40008c,0);CHECK(!failure.empty() && !read(0x40008c));failure.clear();
    write(0x400750,0xea0008);read(0x400754);CHECK(!failure.empty());
}

static void test_pgraph_context_admission() {
    // Exercise both parser selections with directly addressable memory: the
    // run/bulk optimization must not bypass an original-driver context hold.
    for(bool batching : {false,true}) {
        std::string failure;
        xlle::Nv2a gpu([&](const std::string& why){failure=why;});
        std::vector<uint8_t> ram(8192);
        gpu.set_pfifo_run_batching(batching);
        gpu.set_mem_access(
            [&](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;},
            [&](uint32_t a,size_t n)->uint8_t*{return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;});
        auto read=[&](uint32_t reg){return gpu.mmio_read(0xfd000000u+reg,4);};
        auto write=[&](uint32_t reg,uint32_t value){gpu.mmio_write(0xfd000000u+reg,4,value);};
        bool irq=false;
        gpu.set_irq([&](int line,bool level){CHECK(line==3);irq=level;},3);
        const uint32_t packet[]={0x40000,1,0x401a4,2,0x81d6c,4096,0x12345678};
        std::memcpy(ram.data(),packet,sizeof(packet));put32(ram,4096,0xabcdef01);
        const uint32_t setup[][2]={
            {0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700010,2},{0x700014,0x80010030},
            {0x700300,0x3d},{0x700304,8191},{0x700308,0},
            {0x700180,0x3d},{0x700184,8191},{0x700188,0},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,1},
            {0x140,1},{0x400140,0x1000},
            {0x400144,0x10000100},{0x400148,0x03000000},{0x400764,0x08000000}};
        for(auto& r:setup)write(r[0],r[1]);
        write(0x800040,sizeof(packet));
        CHECK(irq && read(0x400100)==0x1000 && read(0x100)==0x1000);
        CHECK(!(read(0x400704)&0x01f00000)); // trapped channel is 0
        CHECK(read(0x400764)&0x10000000);
        CHECK(get32(ram,4096)==0xabcdef01 && gpu.push_methods()==0);
        // FIFO may prefetch while PGRAPH is idle. Neither GET nor STATUS=0
        // can stand in for the semaphore completion in guest memory.
        CHECK(read(0x800044)==sizeof(packet) && read(0x400700)==0);
        write(0x400100,0x1000);
        CHECK(!irq && get32(ram,4096)==0xabcdef01);
        write(0x400144,0x10010100); // valid, but still the wrong channel
        write(0x400764,0x08000000);
        CHECK(irq && get32(ram,4096)==0xabcdef01 && gpu.push_methods()==0);
        write(0x400100,0x1000);write(0x400148,0);
        write(0x400144,0x00010100); // device disabled, even with valid channel
        write(0x400764,0x08000000);
        CHECK(!irq && get32(ram,4096)==0xabcdef01);
        write(0x400144,0x10010100);write(0x400720,1);
        CHECK(get32(ram,4096)==0x12345678 && gpu.semaphore_writes()==1);
        CHECK(gpu.push_methods()==4 && read(0x400700)==0);
        write(0x400764,0x28000000); // explicit FIFO hold
        put32(ram,sizeof(packet),0x41d70);put32(ram,sizeof(packet)+4,0x98765432);
        write(0x800040,sizeof(packet)+8);
        CHECK(get32(ram,4096)==0x12345678 && gpu.semaphore_writes()==1 && !irq);
        write(0x400764,0x08000000);
        CHECK(get32(ram,4096)==0x98765432 && gpu.semaphore_writes()==2 && !irq);
        CHECK(read(0x003280)==1 && read(0x003254)==1);
        // REF_CNT is a real ordered completion counter, mirrored to USER.
        // Neither publication of PUT nor a CPU read may advance it early.
        write(0x400720,0);
        put32(ram,sizeof(packet)+8,0x40050);put32(ram,sizeof(packet)+12,0x11223344);
        write(0x800040,sizeof(packet)+16);
        CHECK(read(0x003248)==0 && read(0x800048)==0);
        write(0x400720,1);
        CHECK(read(0x003248)==0x11223344 && read(0x800048)==0x11223344);
        // Restore a partially parsed non-incrementing method, then resume on
        // its data word. A detached DMA_STATE shadow would parse data as a
        // header or dispatch to the wrong method/subchannel.
        write(0x003200,0);
        write(0x003228,0x48051); // count=1, subchannel=4, method=0x50, noninc
        write(0x00324c,0x101);
        CHECK(read(0x003228)==0x48051 && read(0x00324c)==0x101);
        put32(ram,sizeof(packet)+16,0x55667788);
        write(0x800040,sizeof(packet)+20);
        write(0x003200,1);
        CHECK(read(0x003248)==0x55667788 && read(0x800048)==0x55667788);
        CHECK(read(0x003228)==0x8051 && read(0x00324c)==0x101);
        write(0x003200,0);
        write(0x003204,0x101);write(0x003248,0xaabbccdd);
        CHECK(read(0x003248)==0xaabbccdd && read(0x810048)==0xaabbccdd);
        CHECK(read(0x800048)==0x55667788 && read(0x003228)==0 && read(0x00324c)==0);
        write(0x003204,0x100);
        CHECK(read(0x003248)==0x55667788 && read(0x003228)==0x8051);
        CHECK(read(0x003260)==0 && read(0x003264)==0 && read(0x003268)==0 && read(0x00326c)==0);
        CHECK(failure.empty());
        write(0x003228,0x20000000);CHECK(!failure.empty() && read(0x003228)==0x8051);failure.clear();
        write(0x00326c,1);CHECK(!failure.empty() && read(0x00326c)==0);
    }
}

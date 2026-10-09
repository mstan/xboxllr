// Authored bounded initialization cases; nxdk pbkit names/control sequence and
// xemu reset PLL coefficients are independent of the original title AOT.
static void test_gpu_initialization_controls() {
    std::string failure;
    xlle::Nv2a gpu([&](const std::string& why){failure=why;});
    auto read=[&](uint32_t reg,int size=4){return gpu.mmio_read(0xfd000000u+reg,size);};
    auto write=[&](uint32_t reg,uint32_t value){gpu.mmio_write(0xfd000000u+reg,4,value);};
    write(0x1830,0);CHECK(read(0x1830)==0);
    write(0x1830,0x12345000);CHECK(read(0x1830)==0x12345000);
    write(0x1830,0);write(0x180c,0xf800);CHECK(read(0x180c)==0xf800);
    CHECK(read(0x1800)==0x02a010de && read(0x1808)==0x030000a1);
    write(0x200,0xffffffff);CHECK(read(0x200)==0xffffffff);
    CHECK(read(0x680500)==0x11c01 && read(0x680501,1)==28);
    CHECK(read(0x680502,1)==1 && read(0x680500,2)==0x1c01);
    // pbkit's original-derived PLL formula must obtain the same source used
    // by the maintained PTIMER clock (not a title-specific guessed rate).
    uint32_t pll=read(0x680500);
    CHECK(((16666666ull*((pll>>8)&255))/(1u<<((pll>>16)&7)))/(pll&255)==233333324);
    CHECK(read(0x680504)==0 && read(0x680508)==0x3c20d);
    CHECK(read(0x100204)==0 && read(0x1218)==0);
    write(0x100214,0);CHECK(read(0x100214)==0);
    write(0x8088,0x04000400);write(0x808c,0x04000400);
    write(0x8910,0x1000);write(0x8938,0x100000);write(0x8704,1);
    CHECK(read(0x8088)==0x04000400 && read(0x808c)==0x04000400);
    CHECK(read(0x8910)==0x1000 && read(0x8938)==0x100000 && read(0x8700)==0);
    CHECK(read(0x8704)==0 && read(0x8100)==0);
    write(0x600804,2);CHECK(read(0x600804)==2);
    write(0x400140,1u<<20);write(0x400720,1);
    write(0x400780,0x110a);CHECK(read(0x400780)==0x110a);
    write(0x7110a0,0xdeadbeef);CHECK(read(0x7110a0)==0xdeadbeef);
    write(0x200,0xffffefff);
    CHECK(read(0x200)==0xffffefff && read(0x400140)==0 && read(0x400720)==0);
    CHECK(read(0x400780)==0 && read(0x7110a0)==0xdeadbeef); // engine reset preserves RAM
    write(0x200,0xffffffff);write(0x400720,1);CHECK(read(0x400720)==1);
    CHECK(failure.empty());
    write(0x1830,1);CHECK(!failure.empty() && read(0x1830)==0);
    failure.clear();write(0x200,0);CHECK(!failure.empty() && read(0x200)==0xffffffff);
    failure.clear();write(0x8700,1);CHECK(!failure.empty() && read(0x8700)==0);
    failure.clear();write(0x100214,1);CHECK(!failure.empty() && read(0x100214)==0);
}

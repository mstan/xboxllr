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

static void test_gpu_platform_bus() {
    std::string failure;
    auto fault=[&](const std::string& why){failure=why;};
    xlle::Nv2a gpu(fault);
    xlle::PciHost pci(fault);
    auto attach=[&](uint8_t reg,int size,bool write,uint32_t value) {
        if(write){gpu.pci_write(reg,size,value);return 0u;}
        return gpu.pci_read(reg,size);
    };
    CHECK(pci.attach_function(1,0,0,attach));
    CHECK(!pci.attach_function(1,0,0,attach) && !pci.attach_function(0,0,0,attach));
    pci.io_write(0xcf8,4,0x80010000);
    CHECK(pci.io_read(0xcfc,4)==0x02a010de);
    CHECK(pci.io_read(0xcfc,2)==0x10de && pci.io_read(0xcfe,2)==0x02a0);
    pci.io_write(0xcfc,4,0);CHECK(pci.io_read(0xcfc,4)==0x02a010de);
    pci.io_write(0xcf8,4,0x80010004);pci.io_write(0xcfc,1,4);
    CHECK(gpu.mmio_read(0xfd001804,4)==4);
    gpu.mmio_write(0xfd001804,4,0);CHECK(pci.io_read(0xcfc,4)==0);
    pci.io_write(0xcf8,4,0x8001000c);pci.io_write(0xcfd,1,0xf8);
    CHECK(gpu.mmio_read(0xfd00180c,4)==0xf800);
    pci.io_write(0xcf8,4,0x8001004c);pci.io_write(0xcff,1,0x1f);
    CHECK(gpu.mmio_read(0xfd00184c,4)==0x1f000000);
    gpu.mmio_write(0xfd00184c,4,0x07000000);
    CHECK(pci.io_read(0xcff,1)==7 && pci.io_read(0xcfc,4)==0x07000000);
    pci.io_write(0xcf8,4,0x80010800);CHECK(pci.io_read(0xcfc,4)==0xffffffff);
    pci.io_write(0xcf8,4,0x00010000);CHECK(pci.io_read(0xcfc,4)==0xffffffff);
    CHECK(failure.empty());
    pci.io_read(0xcff,2);CHECK(!failure.empty());failure.clear();
    gpu.mmio_write(0xfd00184c,4,0x100);CHECK(!failure.empty());failure.clear();
    CHECK(gpu.mmio_read(0xfd00184c,4)==0x07000000);
    xlle::McpxFieldInput field(fault);
    field.write(1);field.set_level(false);CHECK(field.read()==1 && !field.input());
    field.set_level(true);CHECK(field.read()==0x61 && field.input());
    for(unsigned i=0;i<50;++i)CHECK(field.read()==0x61); // reads never toggle
    field.set_level(false);CHECK(field.read()==0x41 && !field.input());
    field.write(3);CHECK(field.input()); // selected stored latch outlives pad
    field.write(0x43);CHECK(field.read()==3 && !field.input());
    field.write(0);field.set_level(false);CHECK(field.input()); // active low
    field.write(0x41);CHECK(field.read()==1);
    field.write(5);CHECK(!failure.empty() && field.read()==1);
}

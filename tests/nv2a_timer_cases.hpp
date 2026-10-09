// Authored cases from the envytools PTIMER register/counter description.
// Explicit source edges make expectations independent of host speed/MMIO count.
static void test_ptimer_reference() {
    std::string failure;
    xlle::Nv2a gpu([&](const std::string& why){failure=why;});
    bool irq=false;
    gpu.set_irq([&](int line,bool level){CHECK(line==3);irq=level;},3);
    auto read=[&](uint32_t offset){return gpu.mmio_read(0xfd009000u+offset,4);};
    auto write=[&](uint32_t offset,uint32_t value){gpu.mmio_write(0xfd009000u+offset,4,value);};
    write(0x200,3);write(0x210,2);write(0x420,64);
    gpu.mmio_write(0xfd000140,4,1);
    gpu.set_timer_clock(1);
    CHECK(read(0x400)==0 && read(0x100)==0);
    gpu.set_timer_clock(2);
    CHECK(read(0x400)==32 && read(0x410)==0);
    CHECK(read(0x400)==32); // repeated reads cannot fabricate progress
    gpu.set_timer_clock(3);
    CHECK(read(0x400)==64 && read(0x100)==1 && !irq);
    CHECK(gpu.mmio_read(0xfd000100,4)==0); // alarm latched while masked
    write(0x140,1);
    CHECK(irq && gpu.mmio_read(0xfd000100,4)==(1u<<20));
    write(0x100,0);CHECK(irq);
    write(0x100,1);CHECK(!irq && !read(0x100));
    gpu.set_timer_clock(3);CHECK(!irq); // acknowledge at same clock stays clear
    write(0x210,0);gpu.set_timer_clock(300);
    CHECK(read(0x400)==64 && !irq); // paused clock preserves whole ticks
    write(0x200,1);write(0x210,1);write(0x420,0xffffffe1);
    CHECK(read(0x420)==0xffffffe0);
    constexpr uint64_t period=uint64_t{1}<<27;
    gpu.set_timer_clock(300+period-3);
    CHECK(read(0x400)==0xffffffe0 && read(0x410)==0 && irq);
    write(0x100,1);gpu.set_timer_clock(300+period-2);
    CHECK(!read(0x400) && read(0x410)==1 && !irq);
    write(0x420,64);gpu.set_timer_clock(300+period+2);
    CHECK(read(0x400)==128 && read(0x100)==1 && irq); // crossed between slices
    write(0x140,0);CHECK(!irq && read(0x100)==1);
    write(0x100,0xffffffff);CHECK(!read(0x100));
    gpu.set_timer_clock(300+period*2+2);
    CHECK(read(0x100)==1); // repeating low-counter alarm after wrap
    CHECK(failure.empty());
    // Nouveau sets a real counter epoch through TIME_HIGH/TIME_LOW. Write
    // halves independently, preserving the other half and the source phase.
    write(0x210,0);write(0x100,1);
    write(0x410,0xe1234567);write(0x400,0xffffffef);
    CHECK(read(0x410)==0x01234567 && read(0x400)==0xffffffe0);
    gpu.set_timer_clock(300+period*2+9);
    CHECK(read(0x400)==0xffffffe0 && !read(0x100));
    write(0x210,1);gpu.set_timer_clock(300+period*2+10);
    CHECK(read(0x400)==0 && read(0x410)==0x01234568);
    write(0x410,0);CHECK(read(0x400)==0 && read(0x410)==0);
    write(0x400,64);CHECK(read(0x100)==1); // actual equality, not skipped time
    write(0x200,0);CHECK(!failure.empty() && read(0x200)==1);

    // Independent fresh device lifetimes do not retain another device's time.
    xlle::Nv2a fresh([&](const std::string& why){failure=why;});
    CHECK(!fresh.mmio_read(0xfd009400,4) && !fresh.mmio_read(0xfd009100,4));
    failure.clear();fresh.set_timer_clock(100);fresh.set_timer_clock(99);
    CHECK(!failure.empty());
}

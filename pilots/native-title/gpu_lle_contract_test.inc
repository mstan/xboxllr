/* Transport qualification with real PFIFO semaphore DMA into original-kernel
 * pages. Original title driver/callback behavior is a separate integration gate. */
__declspec(noinline) static uint32_t gpu_test_read(uint32_t offset)
{return *(volatile uint32_t*)(memory+0xfd000000ull+offset);}
__declspec(noinline) static void gpu_test_write(uint32_t offset,uint32_t value)
{*(volatile uint32_t*)(memory+0xfd000000ull+offset)=value;}
static uint32_t gpu_test_smb_port(uint16_t port,int write,uint32_t value)
{
    CHECK(!gpu_lle_port(port,1,write,&value));return value;
}
static void gpu_test_encoder_write(uint8_t reg,uint8_t value)
{
    gpu_test_smb_port(0xc000,1,0x3f);
    gpu_test_smb_port(0xc004,1,0x8a);
    gpu_test_smb_port(0xc008,1,reg);
    gpu_test_smb_port(0xc006,1,value);
    gpu_test_smb_port(0xc002,1,0x0a); // byte-data, polled; actual device operation
    CHECK(gpu_test_smb_port(0xc000,0,0)==0x10);
    gpu_test_smb_port(0xc000,1,0x10);
}
static uint8_t gpu_test_encoder_read(uint8_t reg)
{
    gpu_test_smb_port(0xc000,1,0x3f);
    gpu_test_smb_port(0xc004,1,0x8b);
    gpu_test_smb_port(0xc008,1,reg);
    gpu_test_smb_port(0xc002,1,0x0a);
    CHECK(gpu_test_smb_port(0xc000,0,0)==0x10);
    uint8_t result=(uint8_t)gpu_test_smb_port(0xc006,0,0);
    gpu_test_smb_port(0xc000,1,0x10);return result;
}
static void gpu_test_av_blanking(void)
{
    uint32_t known=0,high=0;
    CHECK(gpu_lle.av_outputs(gpu_lle.device,&known,&high)==-1 && known==0);
    // This fixture starts before encoder programming and observes raw digital
    // output. It does not equate raw scanout with filtered/encoded TV output.
    uint32_t base=physical_allocate(4096,0,0x03ffffff,4096,PAGE_READWRITE);
    CHECK(base);MEM32(base)=0xff302010;MEM32(base+4)=0xff605040;
    gpu_test_write(0x600800,base&0x03ffffff);
    gpu_test_write(0x680820,1);gpu_test_write(0x680800,0);
    gpu_lle.write(gpu_lle.device,0xfd6013d4,1,0x13);
    gpu_lle.write(gpu_lle.device,0xfd6013d5,1,1);
    gpu_lle.write(gpu_lle.device,0xfd6013d4,1,0x28);
    gpu_lle.write(gpu_lle.device,0xfd6013d5,1,3);
    for(unsigned wide=0;wide<2;++wide) {
        pilot_cpu_out8(0x80d6,(uint8_t)(4+wide));
        pilot_cpu_out8(0x80d3,5);pilot_cpu_out8(0x80d8,4);
        CHECK(gpu_lle.av_outputs(gpu_lle.device,&known,&high)==(wide?0:-2));
        for(unsigned repeat=0;repeat<2;++repeat) {
            uint32_t args[]={0,9,1,0};call_kernel(2,args,4);
            uint8_t pixels[8];uint32_t width=0,height=0;
            CHECK(gpu_lle.scanout(gpu_lle.device,pixels,sizeof(pixels),&width,&height));
            const uint8_t black[]={0,0,0,255,0,0,0,255};
            CHECK(width==2 && height==1 && !memcmp(pixels,black,8));
            CHECK(MEM32(base)==0xff302010 && MEM32(base+4)==0xff605040);
            args[2]=0;call_kernel(2,args,4);
            CHECK(gpu_lle.scanout(gpu_lle.device,pixels,sizeof(pixels),&width,&height));
            const uint8_t expected[]={0x30,0x20,0x10,255,0x60,0x50,0x40,255};
            CHECK(!memcmp(pixels,expected,8));
            CHECK(gpu_lle.av_outputs(gpu_lle.device,&known,&high)==(wide?1:2));
            CHECK(known==((1u<<19)|(1u<<22)|(1u<<24)) && high==(wide<<22));
        }
    }
    gpu_test_write(0x600800,0);physical_free(base);
    CHECK(!gpu_lle.error(gpu_lle.device));
}
#ifdef PILOT_GPU_FILES_WORKER
static void gpu_test_encoder_hal_write(uint8_t reg,uint8_t value)
{
    uint32_t args[]={0x8a,reg,0,value};call_kernel(50,args,4);
    CHECK(g_eax==0 && gpu_test_smb_port(0xc000,0,0)==0 && pilot_usb_pic_vector(0)<0);
}
static uint32_t gpu_test_encoder_hal_read(uint8_t reg)
{
    MEM32(0x15000)=0x11223344;MEM32(0x15004)=0xcccccccc;MEM32(0x15008)=0x55667788;
    uint32_t args[]={0x8a,reg,0,0x15004};call_kernel(45,args,4);
    CHECK(g_eax==0 && gpu_test_smb_port(0xc000,0,0)==0 && pilot_usb_pic_vector(0)<0);
    CHECK(MEM32(0x15000)==0x11223344 && MEM32(0x15008)==0x55667788);
    return MEM32(0x15004);
}
static void gpu_test_encoder_reset(void)
{
    // Original kernel HAL routines, IRQ11, ISR and DPC execute end to end.
    // These are explicit fixture inputs, never a title warm-state shortcut.
    for(unsigned reset=0;reset<2;++reset) {
        gpu_test_encoder_hal_write(0xba,0x80);
        gpu_test_encoder_hal_write(0xba,0x3f);
        gpu_test_encoder_hal_write(0x6c,0x46);
        uint64_t rates[6];uint32_t driven=0xcccccccc;
        CHECK(gpu_lle.encoder_clocks(gpu_lle.device,rates,&driven));
        CHECK(!driven && rates[0]==57744984375ull && rates[1]==2048);
        CHECK(rates[2]==rates[0] && rates[3]==rates[1]);
        gpu_test_encoder_hal_write(0xc4,1);
        CHECK(gpu_test_encoder_hal_read(0xc4)==1);
        CHECK(gpu_lle.encoder_clocks(gpu_lle.device,rates,&driven) && driven==1);
        gpu_test_encoder_hal_write(0x3a,8); // only the physical CLKO pin divides
        CHECK(gpu_lle.encoder_clocks(gpu_lle.device,rates,&driven));
        CHECK(rates[0]==57744984375ull && rates[1]==2048);
        CHECK(rates[4]==57744984375ull && rates[5]==4096);
        gpu_test_encoder_hal_write(0x3a,0);
        gpu_test_encoder_hal_write(0x9c,0x68);
        gpu_test_encoder_hal_write(0x9e,0xda);
        gpu_test_encoder_hal_write(0xa0,0x0a);
        CHECK(gpu_lle.encoder_clocks(gpu_lle.device,rates,&driven));
        CHECK(rates[0]==12502828125ull && rates[1]==512);
        gpu_test_encoder_hal_write(0xc4,0);
        CHECK(gpu_lle.encoder_clocks(gpu_lle.device,rates,&driven) && !driven);
        CHECK(gpu_test_encoder_hal_read(0xba)==0x3f);
        CHECK(gpu_test_encoder_hal_read(0x34)==0 && gpu_test_encoder_hal_read(0xc8)==0);
        CHECK(gpu_test_encoder_hal_read(0x8a)==0x10 && gpu_test_encoder_hal_read(0x96)==0x36);
        gpu_test_encoder_hal_write(0xc8,0x9b);
        gpu_test_encoder_hal_write(0xd4,0x95);
        gpu_test_encoder_hal_write(0x38,0x10);
        CHECK(gpu_test_encoder_hal_read(0xd4)==0xd5);
        gpu_test_encoder_hal_write(0xb8,1);
        CHECK(gpu_test_encoder_hal_read(0x8a)==0xb0 && gpu_test_encoder_hal_read(0xa2)==0x24);
        CHECK(gpu_test_encoder_hal_read(0xc8)==0x9b && gpu_test_encoder_hal_read(0xd4)==0x95);
    }
}
#endif
static void gpu_test_av(void)
{
    gpu_test_av_blanking();
    const uint32_t cases[][2]={{6,0x004e0101},{13,0},{16,0},{17,0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        MEM32(0x15000)=0x11223344;MEM32(0x15004)=0xcdcdcdcd;MEM32(0x15008)=0x55667788;
        uint32_t args[]={0,cases[i][0],0,0x15004};call_kernel(2,args,4);
        CHECK(MEM32(0x15004)==cases[i][1]);
        CHECK(MEM32(0x15000)==0x11223344 && MEM32(0x15008)==0x55667788);
    }
    for(unsigned level=0;level<2;++level) {
        gpu_lle.field(gpu_lle.device,level);
        uint32_t args[]={0,15,0,0x15004},result=0;
        // No host tick between driving this external pin and sampling it.
        CHECK(!pilot_io_device_lle(2,memory,args,&result));
        CHECK(MEM32(0x15004)==(level^1));
    }
#ifdef PILOT_GPU_FILES_WORKER
    gpu_test_encoder_reset();
    // Establish actual control state through SMBus, rather than pretending a
    // RAM snapshot contains encoder state. This setup is fixture-only.
    gpu_test_encoder_write(0x6c,0x46);
    const uint8_t expected[]={0xf5,0x89,0x92,0x9b,0x80,0x80};
    for(unsigned filter=0;filter<6;++filter) {
        gpu_test_encoder_write(0xc8,0xb5);gpu_test_encoder_write(0x34,0x80);
        uint32_t args[]={0,11,filter,0};call_kernel(2,args,4);
        CHECK(gpu_test_smb_port(0xc000,0,0)==0 && pilot_usb_pic_vector(0)<0);
        CHECK(gpu_test_encoder_read(0xc8)==expected[filter]);
        CHECK(gpu_test_encoder_read(0x34)==((filter==0 || filter==5)?0x80:0));
        CHECK(pilot_usb_pic_vector(0)<0); // original ISR cleared device status/EOI
    }
    for(unsigned soften=0;soften<2;++soften) {
        gpu_test_encoder_write(0x96,0xcd);
        uint32_t args[]={0,14,soften,0};call_kernel(2,args,4);
        CHECK(gpu_test_smb_port(0xc000,0,0)==0 && pilot_usb_pic_vector(0)<0);
        CHECK(gpu_test_encoder_read(0x96)==(soften?0x1d:0x0d));
        CHECK(pilot_usb_pic_vector(0)<0);
    }
#endif
}
static void test_gpu_lle_transport(void)
{
    install_fault_handler();
#ifdef PILOT_GPU_FILES_WORKER
    // Same captured original worker setup as the actual title: original
    // priority/critical-region calls keep unrelated boot workers suspended.
    CHECK(!pilot_io_device_files(memory));
#endif
    uint32_t query[]={UINT32_MAX,0x15000};MEM32(0x15000)=0xcdcdcdcd;
    call_kernel(168,query,2);uint32_t top=g_eax;
    CHECK(top==0x83ff0000 && MEM32(0x15000)==0x10000);
    uint32_t first=top-0x10000,keep=top-0x5000,protect=0;
    CHECK(!pilot_io_device_lle(179,memory,&first,&protect) && protect==0x204);
    query[0]=0x5000;call_kernel(168,query,2);CHECK(g_eax==top);
    CHECK(!pilot_io_device_lle(179,memory,&first,&protect) && protect==0);
    CHECK(!pilot_io_device_lle(179,memory,&keep,&protect) && protect==0x204);
    call_kernel(168,query,2);CHECK(g_eax==top && MEM32(0x15000)==0x10000);
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        // This transport fixture supplies explicit FIELD levels. The actual
        // title profile instead owns its board clock and rejects injection.
        gpu_lle_open_profile_clock(0,1);
        gpu_test_av();
        uint32_t bootstrap_va=0x80000000u,bootstrap_pa=UINT32_MAX;
        CHECK(!pilot_io_device_lle(173,memory,&bootstrap_va,&bootstrap_pa) && bootstrap_pa==0);
        if(lifetime)CHECK(MEM32(bootstrap_va)==0xdeadbeef); // console lifetime, not GPU lifetime
        uint8_t mask=pilot_cpu_in8(0xa1);
        pilot_cpu_out8(0xa1,(uint8_t)(mask^0x80));
        CHECK(pilot_cpu_in8(0xa1)==(uint8_t)(mask^0x80));
        pilot_cpu_out8(0xa1,mask);CHECK(pilot_cpu_in8(0xa1)==mask);
        pilot_cpu_out8(0x80c0,1);
        CHECK((pilot_cpu_in8(0x80c0)&0x1f)==1);
        /* Actual original HalReadWritePCISpace, including its CF8/CFC IN/OUT
         * and partial-buffer handling. Both config routes own the same state. */
        uint32_t config[]={1,0,0,0x15010,4,0};
        MEM32(0x15010)=0;MEM32(0x15014)=0xaabbccdd;
        call_kernel(46,config,6);
        CHECK(MEM32(0x15010)==0x02a010de && MEM32(0x15014)==0xaabbccdd);
        config[2]=0x4c;MEM32(0x15010)=0x1f000000;config[5]=1;
        call_kernel(46,config,6);CHECK(gpu_test_read(0x184c)==0x1f000000);
        gpu_test_write(0x184c,0x07000000);
        config[2]=0x4f;config[4]=1;config[5]=0;MEM32(0x15010)=0xabcdef00;
        call_kernel(46,config,6);CHECK(MEM32(0x15010)==0xabcdef07);
        config[2]=0x0d;config[5]=1;MEM8(0x15010)=0xf8;
        call_kernel(46,config,6);CHECK(gpu_test_read(0x180c)==0xf800);
        config[0]=2;config[2]=0;config[4]=4;config[5]=0;
        call_kernel(46,config,6);CHECK(MEM32(0x15010)==0xffffffff);
        gpu_test_write(0x1804,4);CHECK(gpu_test_read(0x1804)==4);
        uint32_t base=0;
        if(lifetime==2) {
            uint32_t bytes=8192;CHECK(!pilot_io_device_lle(165,memory,&bytes,&base));
            CHECK(base && !physical_find(base,1));
        } else base=physical_allocate(8192,0,0x03ffffff,4096,PAGE_READWRITE);
        CHECK(base);uint32_t pa=base&0x03ffffff;
        const uint32_t packet[]={0x40000,1,0x401a4,2,0x81d6c,4096,0x12345678};
        memcpy(memory+base,packet,sizeof(packet));MEM32(base+4096)=0xabcdef01;
        // Original/nxdk bootstrap: the pusher starts at physical zero and
        // follows a real jump into owned contiguous command storage.
        MEM32(bootstrap_va)=pa|1u;
        uint32_t saved_eax=g_eax,saved_edx=g_edx;
        pilot_cpu_wbinvd();
        CHECK(g_eax==saved_eax && g_edx==saved_edx);
        uint32_t bootstrap_word=0;
        CHECK(gpu_lle_dma_read(0,&bootstrap_word,4) && bootstrap_word==(pa|1u));
        CHECK(!gpu_lle_dma_read(4094,&bootstrap_word,4));
        const uint32_t setup[][2]={
            {0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700010,2},{0x700014,0x80010030},
            {0x700300,0x3d},{0x700304,8191},{0x700308,pa},
            {0x700180,0x3d},{0x700184,0x03ffffff},{0x700188,0},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,0}};
        for(unsigned i=0;i<sizeof(setup)/sizeof(setup[0]);++i)gpu_test_write(setup[i][0],setup[i][1]);
        gpu_test_write(0x800040,pa+sizeof(packet));
        CHECK(MEM32(base+4096)==0xabcdef01); // held PGRAPH cannot complete DMA
        gpu_test_write(0x400720,1);
        CHECK(MEM32(base+4096)==0x12345678);
        CHECK(gpu_test_read(0x800044)==pa+sizeof(packet));
        bootstrap_word=0xdeadbeef;
        CHECK(gpu_lle_dma_write(0,&bootstrap_word,4) && MEM32(bootstrap_va)==bootstrap_word);
        DWORD bootstrap_protection=0;
        CHECK(VirtualProtect(memory+bootstrap_va,4096,PAGE_READONLY,&bootstrap_protection));
        bootstrap_word=0x11111111;
        CHECK(!gpu_lle_dma_write(0,&bootstrap_word,4) && MEM32(bootstrap_va)==0xdeadbeef);
        CHECK(VirtualProtect(memory+bootstrap_va,4096,bootstrap_protection,&bootstrap_protection));
        uint32_t observed=0;
        CHECK(gpu_lle_dma_read(pa+4096,&observed,4) && observed==0x12345678);
        CHECK(!gpu_lle_dma_write(0x04000000,&observed,4));
        if(lifetime==2) {
            uint32_t args[]={base+4096,4096,PAGE_READONLY},result=0;
            CHECK(!pilot_io_device_lle(182,memory,args,&result));
        } else physical_protect(base+4096,4096,PAGE_READONLY);
        CHECK(!gpu_lle_dma_write(pa+4096,&observed,4));
        if(lifetime==2) {
            uint32_t args[]={base+4096,4096,PAGE_READWRITE},result=0;
            CHECK(!pilot_io_device_lle(182,memory,args,&result));
            CHECK(!pilot_io_device_lle(171,memory,&base,&result));
        } else {physical_protect(base+4096,4096,PAGE_READWRITE);physical_free(base);}
        CHECK(!gpu_lle_dma_read(pa+4096,&observed,4));
        CHECK(!gpu_lle.error(gpu_lle.device));
        /* This fixture has consumed its entire FIFO, no connected ISR and no
         * enabled interrupt. It does not claim general title teardown proof. */
        CHECK(!pilot_io_device_work_unbind(gpu_lle_sample));
        gpu_lle.destroy(gpu_lle.device);gpu_lle.device=NULL;
        CHECK(!pilot_io_device_bus_memory_bind(0xf0000000u,0x04000000u,NULL));
        CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,NULL));
        pilot_io_device_port_bind(0,0,NULL);
        CHECK(FreeLibrary(gpu_lle.library));gpu_lle.library=NULL;
    }
    fprintf(stderr,"PASS: original GPU reservation and physical-zero translation; real shared bootstrap jump/cache publication; compiled MMIO, owned DMA, held/released semaphore, protection/release rejection, three transport lifetimes\n");
}

static void gpu_test_completed_av_mode(uint32_t phase)
{
    CHECK(gpu_lle.read(gpu_lle.device,0xfd680880,4)==0x21101100);
    CHECK(gpu_lle.read(gpu_lle.device,0xfd6806a0,4)==((phase^1u)*0x11u));
    uint64_t rates[6];uint32_t driven=0;
    CHECK(gpu_lle.encoder_clocks(gpu_lle.device,rates,&driven) && driven==1);
    CHECK(rates[0]==12502828125ull && rates[1]==512 && rates[2]==rates[0] && rates[3]==rates[1]);
    // WSS control writes after the timing reset must preserve the FIELD owner.
    uint32_t field=0;const char* field_error=gpu_lle_port(0x80c0,1,0,&field);
    if(field_error)fprintf(stderr,"Post-startup FIELD: %s\n",field_error);
    CHECK(!field_error);
    CHECK(!gpu_lle.error(gpu_lle.device));
    uint32_t known=0,high=0;
    CHECK(gpu_lle.av_outputs(gpu_lle.device,&known,&high)==-2);
    CHECK(known==((1u<<19)|(1u<<22)|(1u<<24)) && high==(1u<<19));
}
static void test_gpu_lle_av_mode_startup(uint32_t phase)
{
    install_fault_handler();
#ifdef PILOT_GPU_FILES_WORKER
    CHECK(!pilot_io_device_files(memory));
#endif
    gpu_lle_open_profile(phase);
    // Startup now reaches actual retrace waits. Use the existing original
    // PIT initialization/shared runtime clock, as the title selection does.
    CHECK(!pilot_io_device_clock_start());
#ifdef PILOT_AUDIO_REFERENCE
    audio_reference_clock_start();
#endif
    // Actual original mode setup executes all tables, waits for three real
    // retraces, resets encoder timing, programs WSS and returns through RET24.
    uint32_t args[]={0xfd000000,0,0,0x12,2560,0},result=0xcccccccc;
    const char *error=pilot_io_device_lle(3,memory,args,&result);
    fprintf(stderr,"Original AV startup: %s result=%08x\n",error?error:"returned",result);
    CHECK(!error && result==0);gpu_test_completed_av_mode(phase);
    // Original AV2 option14 changes 96.YLPF, not V_LINESI or its FIELD epoch.
    // Exercise the actual HAL RMW and original FIELD consumer, then the
    // original same-mode AV3 path (which restores option14 to zero itself).
    for(unsigned enabled=1;enabled!=3;++enabled) {
        uint32_t filter_args[]={0,14,enabled&1,0},field_args[]={0,15,0,0x15004};
        CHECK(!pilot_io_device_lle(2,memory,filter_args,&result));
        MEM32(0x15000)=0x11223344;MEM32(0x15004)=0xcccccccc;MEM32(0x15008)=0x55667788;
        const char *field_error=pilot_io_device_lle(2,memory,field_args,&result);
        if(field_error)fprintf(stderr,"Original filter/FIELD: %s\n",field_error);
        CHECK(!field_error);
        CHECK(MEM32(0x15004)<=1 && MEM32(0x15000)==0x11223344 && MEM32(0x15008)==0x55667788);
    }
    CHECK(!pilot_io_device_lle(3,memory,args,&result) && !result);
    gpu_test_completed_av_mode(phase);
    // Actual first-draw Device+23C8 requests04010101, distinct from the boot
    // default0F010101. This natural reentry must run full AV3 and its final
    // encoder reset; no test-side mode/global invalidation supplies the path.
    args[2]=0x04010101;
    CHECK(!pilot_io_device_lle(3,memory,args,&result) && !result);
    gpu_test_completed_av_mode(phase^1u);
    fprintf(stderr,"PASS: original AV3 returns after actual setup, clock-driven retraces, encoder reset and WSS; connected pixels remain guarded\n");
}

#ifdef PILOT_GPU_FILES_WORKER
static unsigned gpu_test_digital_reads;
static int gpu_test_digital_read(uint32_t address,void *output,size_t bytes)
{
    ++gpu_test_digital_reads;
    const uint32_t base=gpu_lle.launch_framebuffer&0x03ffffffu;
    if(address<base || (uint64_t)address+bytes>(uint64_t)base+640u*480u*4u)return 0;
    memcpy(output,memory+gpu_lle.launch_framebuffer+(address-base),bytes);return 1;
}
static void gpu_test_digital_input(void)
{
    *(FARPROC*)&gpu_lle.digital_snapshot=GetProcAddress(gpu_lle.library,"xlle_nv2a_reference_digital_snapshot");
    CHECK(gpu_lle.digital_snapshot);
    unsigned char *wire=malloc(GPU_DIGITAL_MAX);CHECK(wire);uint32_t required=0;
    memset(wire,0x5a,GPU_DIGITAL_MAX);
    CHECK(!gpu_lle.digital_snapshot(gpu_lle.device,wire,320,&required,gpu_test_digital_read));
    CHECK(required==GPU_DIGITAL_MAX && wire[0]==0x5a && !gpu_test_digital_reads);
    // Actual original AV3 supplies the reached mode. The following RGB pixels
    // and identity DAC upload are explicitly authored component inputs, not
    // original title drawing or evidence of original DAC-upload execution.
    uint32_t args[]={0xfd000000,0,0x04010101,0x12,2560,gpu_lle.launch_framebuffer&0x03ffffffu},result=0xcccccccc;
    CHECK(!pilot_io_device_lle(3,memory,args,&result) && !result);
    gpu_lle.write(gpu_lle.device,0xfd680600,4,0x00100030);
    gpu_lle.write(gpu_lle.device,0xfd6813c8,1,0);
    for(unsigned value=0;value<256;++value)for(unsigned channel=0;channel<3;++channel)
        gpu_lle.write(gpu_lle.device,0xfd6813c9,1,value);
    // Red, green, blue and white; ignore stored alpha at this opaque output.
    const uint32_t colors[]={0x12ff0000,0x3400ff00,0x560000ff,0x78ffffff};
    memcpy(memory+gpu_lle.launch_framebuffer,colors,sizeof(colors));
    CHECK(!gpu_lle.error(gpu_lle.device));
    CHECK(gpu_lle.digital_snapshot(gpu_lle.device,wire,GPU_DIGITAL_MAX,&required,gpu_test_digital_read));
    if(!gpu_context_word(wire+12))fprintf(stderr,"Digital input: %s\n",wire+64);
    CHECK(required==GPU_DIGITAL_MAX && gpu_context_word(wire)==0x31445047 && gpu_context_word(wire+12)==1);
    // AV3 leaves the screen blank for its caller. First verify genuine black
    // without fetching the authored pixels; then explicitly unblank this
    // component fixture. Actual title unblanking is a separate producer.
    CHECK(!gpu_test_digital_reads && gpu_context_word(wire+52)==1);
    CHECK(gpu_context_word(wire+320)==0x00800801);
    gpu_lle.write(gpu_lle.device,0xfd0c03c4,1,1);
    gpu_lle.write(gpu_lle.device,0xfd0c03c5,1,1);
    CHECK(gpu_lle.digital_snapshot(gpu_lle.device,wire,GPU_DIGITAL_MAX,&required,gpu_test_digital_read));
    CHECK(gpu_context_word(wire+12)==1 && !gpu_context_word(wire+52));
    CHECK(gpu_test_digital_reads==480 && gpu_context_word(wire+60)==2);
    // Independent published color-bar vectors, encoded directly as edge words.
    const uint16_t words[]={0xf05,0x15a,0x229,0x136,0x6e2,0x9f0,0x80e,0xb80};
    CHECK(!memcmp(wire+320,words,sizeof(words)));
    const unsigned char rgba[]={254,0,0,255,0,255,1,255,0,0,255,255,255,255,255,255};
    CHECK(!memcmp(wire+320+640*480*4,rgba,sizeof(rgba)));
    CHECK(!gpu_lle.error(gpu_lle.device));free(wire);
}
static void test_gpu_lle_launch_profile(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));gpu_lle_open_profile(0);
    CHECK(gpu_lle_prepare_launch()); // no clock/producer/owned allocation yet
    CHECK(!gpu_lle.launch_framebuffer && !gpu_lle.launch_ready);
    CHECK(!pilot_io_device_clock_start());
#ifdef PILOT_AUDIO_REFERENCE
    audio_reference_clock_start();
#endif
    const char *error=gpu_lle_prepare_launch();
    fprintf(stderr,"Original launch profile: %s\n",error?error:"returned");CHECK(!error);
    CHECK(gpu_lle.launch_ready && !physical_find(gpu_lle.launch_framebuffer,1));
    uint32_t extent=0,state[4]={0};
    CHECK(!pilot_io_device_lle(180,memory,&gpu_lle.launch_framebuffer,&extent) && extent==640u*480u*4u);
    CHECK(!pilot_io_device_av_state(state) && state[2]==gpu_lle.launch_framebuffer);
    uint32_t word=0xffffffffu;CHECK(gpu_lle_dma_read(gpu_lle.launch_framebuffer&0x03ffffffu,&word,4) && !word);
    CHECK(gpu_lle.read(gpu_lle.device,0xfd600800,4)==(gpu_lle.launch_framebuffer&0x03ffffffu));
    gpu_test_completed_av_mode(0);
    // The actual early title option11/parameter5 reads C8 before writing the
    // flicker-filter selection. Its state now comes from the original producer.
    uint32_t args[]={0,11,5,0},result=0xccccccccu;
    CHECK(!pilot_io_device_lle(2,memory,args,&result));
    CHECK(gpu_lle_prepare_launch() && gpu_lle.launch_ready); // no duplicate init
    gpu_test_completed_av_mode(0);
    gpu_test_digital_input();
    fprintf(stderr,"PASS: original pre-title AV producer and saved-display ownership without a native DMA shadow, unchanged shared FIELD and early original encoder consumer; actual title retirement remains required\n");
}
#else
static void test_gpu_lle_launch_profile(void){CHECK(0);}
#endif

#ifdef PILOT_GPU_FILES_WORKER
static void gpu_test_timing_hal_write(uint8_t reg,uint8_t value)
{
    // Clock events are controlled by this board fixture. Calling the original
    // HAL directly avoids the pilot's still-unqualified nominal field source.
    uint32_t args[]={0x8a,reg,0,value},result=0xcccccccc;
    CHECK(!pilot_io_device_lle(50,memory,args,&result) && result==0);
    CHECK(gpu_test_smb_port(0xc000,0,0)==0 && pilot_usb_pic_vector(0)<0);
}
static void gpu_test_timing_field(uint32_t expected)
{
    MEM32(0x15000)=0x11223344;MEM32(0x15004)=0xcccccccc;MEM32(0x15008)=0x55667788;
    uint32_t args[]={0,15,0,0x15004},result=0;
    CHECK(!pilot_io_device_lle(2,memory,args,&result));
    CHECK(MEM32(0x15004)==expected && MEM32(0x15000)==0x11223344 && MEM32(0x15008)==0x55667788);
}
static void test_gpu_lle_encoder_phase_guard(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    gpu_lle_open_profile_clock(0,1); // explicitly supplied clock/sync fixture
    gpu_test_timing_hal_write(0xba,0x80);
    gpu_test_timing_hal_write(0xba,0x20);
    gpu_test_timing_hal_write(0xc6,0x9c);
    gpu_test_timing_hal_write(0xc4,1);
    gpu_test_timing_hal_write(0x6c,0xc6);
    CHECK(gpu_lle.encoder_advance(gpu_lle.device,13,3));
    CHECK(gpu_lle.encoder_advance(gpu_lle.device,7,1));
    CHECK(gpu_lle.encoder_advance(gpu_lle.device,2,0));
    gpu_test_timing_field(0); // FIELD is established independently of color phase
    uint64_t state[7];int signal=-99;
    for(unsigned i=0;i<7;++i)state[i]=0x123456789abcdef0ull+i;
    CHECK(!gpu_lle.encoder_timing(gpu_lle.device,state,&signal));
    const char *error=gpu_lle.error(gpu_lle.device);
    CHECK(error && strstr(error,"qualified raster/color phase"));
    for(unsigned i=0;i<7;++i)CHECK(state[i]==0x123456789abcdef0ull+i);
    CHECK(signal==-99);
    // The rejected observation is terminal; it publishes no partial state.
    fprintf(stderr,"PASS: original AV FIELD works after shifted reset; full timing rejects unknown color/raster phase without changing caller output\n");
}
static void test_gpu_lle_encoder_timing(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    for(unsigned slave=0;slave<2;++slave) for(unsigned lifetime=0;lifetime<3;++lifetime) {
        gpu_lle_open_profile_clock(0,1); // explicitly supplied clock/sync fixture
        gpu_test_timing_hal_write(0xba,0x80);
        gpu_test_timing_hal_write(0xba,slave?0x20:0);
        gpu_test_timing_hal_write(0xc6,0x9c);
        gpu_test_timing_hal_write(0xc4,1);
        gpu_test_timing_hal_write(0x6c,0xc6);
        CHECK(gpu_test_encoder_read(0x6c)==0x46);
        uint64_t state[7];int signal=-99;
        CHECK(gpu_lle.encoder_timing(gpu_lle.device,state,&signal));
        CHECK(state[0]==0 && state[5]==0 && signal==0);
        gpu_test_timing_field(1); // original AV API inverts physical FIELD
        for(unsigned frame=1;frame<=2;++frame) {
            if(slave) {
                for(unsigned line=1;line<=600;++line)
                    CHECK(gpu_lle.encoder_advance(gpu_lle.device,784-((frame==2 && line==1)?2:0),line==600?2:1));
            } else CHECK(gpu_lle.encoder_advance(gpu_lle.device,470400-(frame==2?2:0),0));
            CHECK(gpu_lle.encoder_timing(gpu_lle.device,state,&signal));
            CHECK(state[0]==470400ull*frame && state[3]==((frame&1)?896:0) && state[4]==((frame&1)?262:0));
            gpu_test_timing_field(frame&1); // before the two-CLKI transition
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,1,0));gpu_test_timing_field(frame&1);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,1,0));gpu_test_timing_field((frame&1)^1);
        }
        if(!slave) {
            // An intervening high FIELD still sets the MCPX latch when a
            // batched board advance ends back at low. The guest observes it.
            pilot_cpu_out8(0x80c0,0x43);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,940800,0));
            CHECK((pilot_cpu_in8(0x80c0)&0x60)==0x40);
        }
        if(slave) {
            // Original HAL reset between external sync edges. VSYNC precedes
            // HSYNC by seven actual CLKI cycles, with no GPU phase reset.
            gpu_test_timing_hal_write(0x6c,0xc6);gpu_test_timing_field(1);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,13,3));gpu_test_timing_field(1);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,7,1));gpu_test_timing_field(1);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,1,0));gpu_test_timing_field(1);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,1,0));gpu_test_timing_field(0);
            for(unsigned line=1;line<=600;++line) {
                unsigned clocks=784-(line==1?2:0);
                if(line==600) {
                    CHECK(gpu_lle.encoder_advance(gpu_lle.device,clocks-7,3));gpu_test_timing_field(0);
                    CHECK(gpu_lle.encoder_advance(gpu_lle.device,7,1));
                } else CHECK(gpu_lle.encoder_advance(gpu_lle.device,clocks,1));
            }
            gpu_test_timing_field(0);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,1,0));gpu_test_timing_field(0);
            CHECK(gpu_lle.encoder_advance(gpu_lle.device,1,0));gpu_test_timing_field(1);
        }
        gpu_test_timing_hal_write(0xc6,0xbc);gpu_test_timing_field(0);
        gpu_test_timing_hal_write(0x6c,0xc6);gpu_test_timing_field(0);
        CHECK(gpu_lle.encoder_timing(gpu_lle.device,state,&signal));
        CHECK(state[0]==0 && state[1]==0 && state[2]==0 && state[3]==0 && state[4]==0 && state[5]==0 && signal==1);
        gpu_test_timing_hal_write(0xc4,0);
        CHECK(gpu_lle.encoder_timing(gpu_lle.device,state,&signal) && signal==-2);
        CHECK(!gpu_lle.error(gpu_lle.device));
        if(slave && lifetime==2) {
            MEM32(0x15004)=0xcccccccc;
            uint32_t args[]={0,15,0,0x15004},result=0;
            const char *error=pilot_io_device_lle(2,memory,args,&result);
            CHECK(error && strstr(error,"unknown/high-impedance board level"));
            CHECK(MEM32(0x15004)==0xcccccccc);
            // The original machine deliberately keeps a terminal device fault.
            // No further kernel mutations/unbinds are valid in this process.
            fprintf(stderr,"PASS: original HAL timing reset, actual aligned/shifted slave sync and separated VSYNC, original AV FIELD reads, polarity/gating; five clean teardowns and sixth lifetime rejects undriven FIELD\n");
            return;
        }
        CHECK(!pilot_io_device_work_unbind(gpu_lle_sample));
        gpu_lle.destroy(gpu_lle.device);gpu_lle.device=NULL;
        CHECK(!pilot_io_device_bus_memory_bind(0xf0000000u,0x04000000u,NULL));
        CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,NULL));pilot_io_device_port_bind(0,0,NULL);
        CHECK(FreeLibrary(gpu_lle.library));gpu_lle.library=NULL;
    }
}
#endif

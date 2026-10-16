/* Actual original CALL/RET frames connect the existing AV/HAL device owner.
 * Explicit fixture inputs do not admit the guarded cold display mode. */
#ifdef PILOT_GPU_FILES_WORKER
static void test_gpu_thread_av_lle(int mode_guard)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;uint32_t revision=0;
    CHECK(!usb_access(0xfed00000,0,&revision));thread_device_code();
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<(mode_guard?1u:3u);++life) {
        gpu_lle_open_profile(mode_guard==2?1:0);
        if(mode_guard) {
            const uint32_t context=0x1b000,args[]={0xfd000000,0,0,0x12,2560,0};
            memset(memory+context,0,96);MEM32(context)=0xcccccccc;
            MEM32(context+16)=6;MEM32(context+20)=0xfe000000+3*4;
            memcpy(memory+context+32,args,sizeof(args));MEM32(context+64)=THREAD_DEVICE_CPU;
            uint32_t out=0x19800,birth[]={out,0,THREAD_DEVICE_CPU,context,0};
            CHECK(!event_lle_call(254,birth));uint32_t handle=MEM32(out);
            uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));
            uint32_t join[]={MEM32(out+4),0,0,0,0},result=0xcccccccc;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            fprintf(stderr,"Original thread AV startup boundary: %s\n",error?error:"returned without guard");
            // Shared device work now uses the same published PIT timestamp.
            // Both genuine calling routes reach the next VGA control write.
            CHECK(error && strstr(error,"unmodelled NV2A write at fd0c03c3"));
            CHECK(result==0xcccccccc && MEM32(context)==0xcccccccc && !MEM32(context+4));
            uint32_t known=0,high=0;CHECK(gpu_lle.av_outputs(gpu_lle.device,&known,&high)==-2);
            CHECK(known==((1u<<19)|(1u<<22)|(1u<<24)) && high==(1u<<19));
            fprintf(stderr,"PASS: original-thread AV mode crosses encoder tables and clock routing; VGA guard preserves its caller\n");return;
        }
        /* Expectations independently established by the retained original AV
         * reference and shared-device checks; output DWORD canaries are owned. */
        const uint32_t options[][2]={{6,0x004e0101},{13,0},{16,0},{17,0}};
        for(unsigned i=0;i<sizeof(options)/sizeof(options[0]);++i) {
            MEM32(0x1e400)=0x11223344;MEM32(0x1e404)=0xcdcdcdcd;MEM32(0x1e408)=0x55667788;
            uint32_t args[]={0,options[i][0],0,0x1e404};
            thread_device_call(2,args,4,life);
            CHECK(MEM32(0x1e404)==options[i][1]);
            CHECK(MEM32(0x1e400)==0x11223344 && MEM32(0x1e408)==0x55667788);
        }
        const uint32_t writes[][2]={{0xba,0x80},{0xba,0x3f},{0x6c,0x46},{0xc4,1},{0xc4,0}};
        for(unsigned i=0;i<sizeof(writes)/sizeof(writes[0]);++i) {
            uint32_t write[]={0x8a,writes[i][0],0,writes[i][1]};
            CHECK(!thread_device_call(50,write,4,life));
            CHECK(!gpu_test_smb_port(0xc000,0,0) && pilot_usb_pic_vector(0)<0);
            /* Reset precedes EN_REG_RD programming. The maintained encoder
             * correctly refuses standard readback before the actual 6C write. */
            if(i<2)continue;
            MEM32(0x1e400)=0x11223344;MEM32(0x1e404)=0xcdcdcdcd;MEM32(0x1e408)=0x55667788;
            uint32_t read[]={0x8a,writes[i][0],0,0x1e404};
            CHECK(!thread_device_call(45,read,4,life));
            CHECK(MEM32(0x1e404)==writes[i][1]);
            CHECK(MEM32(0x1e400)==0x11223344 && MEM32(0x1e408)==0x55667788);
            CHECK(!gpu_test_smb_port(0xc000,0,0) && pilot_usb_pic_vector(0)<0);
            if(writes[i][0]==0xc4) {
                uint64_t clocks[6];uint32_t driven=0xcccccccc;
                CHECK(gpu_lle.encoder_clocks(gpu_lle.device,clocks,&driven) && driven==writes[i][1]);
            }
        }
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        CHECK(!pilot_io_device_work_unbind(gpu_lle_sample));CHECK(!gpu_lle.error(gpu_lle.device));
        gpu_lle.destroy(gpu_lle.device);gpu_lle.device=NULL;
        CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,NULL));pilot_io_device_port_bind(0,0,NULL);
        CHECK(FreeLibrary(gpu_lle.library));gpu_lle.library=NULL;
    }
    fprintf(stderr,"PASS: three original-thread AV/HAL lifetimes, 36 actual service returns, owned DWORD outputs and real SMBus IRQ completion/encoder reset\n");
}
#else
static void test_gpu_thread_av_lle(int mode_guard){(void)mode_guard;CHECK(0);}
#endif

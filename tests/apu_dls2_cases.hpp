#include <limits>

static void dls2_math() {
    using namespace xlle;
    const auto c=ApuDls2Coefficients::decode(0x8000f000);
    CHECK(c.frequency==.5 && c.damping==1);
    float impulse[10]={1,0};ApuDls2Lpf f;
    CHECK(f.process(c,impulse,5));
    const float expected[]={.25f,.3125f,.265625f,.17578125f,.0869140625f};
    for(unsigned i=0;i<5;++i)CHECK(impulse[2*i]==expected[i] && impulse[2*i+1]==0);
    // Published unity-coefficient identity, not a decoder-derived golden.
    float identity[]={.125f,-.75f,0,.25f,-.5f,0};
    const auto original=std::vector<float>(identity,identity+6);ApuDls2Lpf unity;
    CHECK(unity.process(ApuDls2Coefficients::decode(0x80000000),identity,3));
    CHECK(std::equal(original.begin(),original.end(),identity));
    const auto actual=ApuDls2Coefficients::decode(0x659083e8);
    CHECK(std::fabs(actual.frequency-.0046265119256265353)<1e-16 && actual.damping==.79345703125);
    // Partitioning has no effect; channel states never bleed across voices.
    std::vector<float> whole(256),split;
    for(unsigned i=0;i<128;++i)whole[2*i]=float(.01*std::sin(i*.17));
    split=whole;ApuDls2Lpf a,b;
    CHECK(a.process(actual,whole.data(),128));
    CHECK(b.process(actual,split.data(),32) && b.process(actual,split.data()+64,96));
    CHECK(whole==split);
    for(unsigned i=0;i<128;++i)CHECK(whole[2*i+1]==0);
    ApuDls2Lpf change;float hit[2]={1,0},tail[2]={0,0};
    CHECK(change.process(c,hit,1));
    CHECK(change.process(ApuDls2Coefficients::decode(0x8000e000),tail,1));
    CHECK(tail[0]==.328125f && tail[1]==0); // coefficient change retains tail
    // Frequency-domain oracle for the linear two-integrator transfer function.
    for(double omega:{.0007,.1}) {
        ApuDls2Lpf tone;double yy=0,xx=0,xy=0,xq=0,sc=0;
        constexpr unsigned start=32768,total=65536;
        for(unsigned i=0;i<total;++i) {
            float frame[2]={float(.001*std::sin(omega*i)),0};
            CHECK(tone.process(actual,frame,1));
            if(i>=start){yy+=frame[0]*std::sin(omega*i);xy+=frame[0]*std::cos(omega*i);
                xx+=std::sin(omega*i)*std::sin(omega*i);xq+=std::cos(omega*i)*std::cos(omega*i);sc+=std::sin(omega*i)*std::cos(omega*i);}
        }
        const double f=actual.frequency,d=actual.damping;
        const double re=1+(f*f+f*d-2)*std::cos(omega)+(1-f*d)*std::cos(2*omega);
        const double im=-(f*f+f*d-2)*std::sin(omega)-(1-f*d)*std::sin(2*omega);
        const double gain=f*f/std::sqrt(re*re+im*im);
        const double determinant=xx*xq-sc*sc;
        const double sine=(yy*xq-xy*sc)/determinant,cosine=(xy*xx-yy*sc)/determinant;
        const double measured=std::sqrt(sine*sine+cosine*cosine)/.001;
        CHECK(std::fabs(measured-gain)<.025*gain);
    }
    float bad[2]={std::numeric_limits<float>::quiet_NaN(),0};
    const auto history=a.channels[0];CHECK(!a.process(actual,bad,1));
    CHECK(a.channels[0].low==history.low && a.channels[0].band==history.band);
}

static void dls2_device() {
    // Identity LPF preserves the existing bypass waveform and source lifetime.
    Fixture bypass,identity(xlle::ApuSignalModel::NominalDls2LinearV1);
    for(auto* f:{&bypass,&identity})f->add(73,1,512,true,0,4096);
    identity.put(Fixture::object(73)+0x18,0x10000);
    identity.put(Fixture::object(73)+0x74,0x80000000);
    for(unsigned i=0;i<4;++i) {
        xlle::ApuVoiceQuantum a,b;CHECK(bypass.quantum(a) && identity.quantum(b));
        CHECK(a.visited==b.visited && a.completed==b.completed && bypass.ram.size()==identity.ram.size());
        CHECK(!std::memcmp(bypass.apu.gp_word(0x1400),identity.apu.gp_word(0x1400),4096));
        CHECK(bypass.source_reads==identity.source_reads);
    }
    CHECK(identity.apu.dls2_filter_frames()==128 && !bypass.apu.dls2_filter_frames());
    // Real VOICE_ON resets both the converter and LPF after ordinary unlink.
    Fixture f(xlle::ApuSignalModel::NominalDls2LinearV1);f.add(73,1,512,true,0,4096);const unsigned o=Fixture::object(73);
    f.put(o+0x18,0x10000);f.put(o+0x74,0x659083e8);
    xlle::ApuVoiceQuantum q;CHECK(f.quantum(q));
    const auto first=std::vector<uint32_t>(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);
    CHECK(f.quantum(q));CHECK(!std::equal(first.begin(),first.end(),f.apu.gp_word(0x1400)));
    const float second=f.sample(0,31);
    const auto frames=f.apu.dls2_filter_frames();f.put(o+0x54,f.word(o+0x54)|(1u<<18));
    CHECK(f.quantum(q) && f.apu.dls2_filter_frames()==frames);
    f.put(o+0x54,f.word(o+0x54)&~(1u<<18));
    CHECK(f.quantum(q) && f.sample(0,31)>second);
    f.method(0x2f8,73);f.method(0x128,73);f.unlink(73);
    f.method(0x2fc,1);f.method(0x120,0x1ffff);f.method(0x124,73);f.method(0x2fc,0);
    CHECK(f.error.empty() && f.quantum(q));
    CHECK(std::equal(first.begin(),first.end(),f.apu.gp_word(0x1400)));
    // Complete-list preflight blocks modulation/adjacent modes before source,
    // cursor, filter state, GP publication or completion can change.
    for(unsigned bad=0;bad<6;++bad) {
        Fixture rejected(bad==5?xlle::ApuSignalModel::Strict:xlle::ApuSignalModel::NominalDls2LinearV1);rejected.add(73,1,128,true,0,4096);
        const unsigned v=Fixture::object(73);rejected.put(v+0x18,bad==1?0x30000:0x10000);
        rejected.put(v+0x74,0x659083e8);
        if(bad==0)rejected.put(v+0x10,0x01000000);
        if(bad==2)rejected.put(v+4,rejected.word(v+4)|(1u<<16));
        if(bad==3)rejected.put(v+0x50,1);
        if(bad==4)rejected.reg(0x202c,Fixture::table); // loses actual VOICE_ON history
        const auto before=rejected.ram;CHECK(!rejected.quantum(q));
        CHECK(!q.published && !rejected.source_reads && rejected.ram==before && !rejected.apu.dls2_filter_frames());
    }
}

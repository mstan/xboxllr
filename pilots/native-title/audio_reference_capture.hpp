/* Private, opt-in observations of the actual caller-driven reference backend.
 * No audio device, worker, clock input or changes to source ownership. Records
 * keep QPC observation time separate from the engine's PIT/audio epoch. */
struct ReferenceCaptureRecord {
    uint32_t magic,version,kind,header;
    uint64_t qpc_100ns,epoch_ticks,engine_id,voice_id,output_frame,fetched;
    uint32_t rate,channels,bits,capacity,offset,bytes,cbo,flags,loop_start,loop_bytes;
};
static_assert(sizeof(ReferenceCaptureRecord)==104,"private reference capture layout");
struct ReferenceCapture {
    FILE *records=nullptr,*output=nullptr;
    uint64_t engine_id=0,epoch=0,output_frames=0,record_bytes=0,record_count=0;
    uint32_t pending_offset=0;Buffer *pending_voice=nullptr;
    ReferenceCaptureRecord pending_record{};
    std::vector<uint8_t> pending;
    std::vector<float> speakers;
    bool enabled=false,epoch_set=false,finished=false,failed=false;
    static constexpr uint64_t frame_limit=600ull*48000,byte_limit=2048ull*1024*1024;
    static uint64_t qpc() {
        LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
        return uint64_t(now.QuadPart/frequency.QuadPart)*10000000+
            uint64_t(now.QuadPart%frequency.QuadPart)*10000000/frequency.QuadPart;
    }
    bool open(uint64_t id) {
        const char *directory=getenv("XLLE_PILOT_REFERENCE_CAPTURE");
        if(!directory || !*directory)return true;
        engine_id=id;enabled=true;char path[2048];
        if(snprintf(path,sizeof(path),"%s/engine-%llu.dma.bin",directory,(unsigned long long)id)>=(int)sizeof(path))return false;
        records=fopen(path,"wbx");if(!records)return false;
        if(snprintf(path,sizeof(path),"%s/engine-%llu.gp.f32",directory,(unsigned long long)id)>=(int)sizeof(path))return false;
        output=fopen(path,"wbx");if(!output)return false;
        pending.reserve(8192);speakers.reserve(48000*6);
        fprintf(stderr,"[REFERENCE CAPTURE] engine=%llu bounded600s/2048MiB; actual DMA bytes and six-channel GP output; separate QPC/PIT epochs\n",(unsigned long long)id);
        return true;
    }
    bool set_epoch(uint64_t ticks) {
        if(!enabled)return true;
        if(epoch_set || output_frames || record_count)return false;
        epoch=ticks;epoch_set=true;return true;
    }
    ReferenceCaptureRecord header(Buffer *b,uint32_t kind,uint32_t offset,uint32_t bytes,uint64_t frame,uint32_t cbo) const {
        return {0x52414c58,1,kind,sizeof(ReferenceCaptureRecord),qpc(),epoch,engine_id,b?b->id:0,frame,b?b->fetched:0,
            b?b->frequency:0,b?b->channels:0,b?b->bits:0,b?b->bytes:0,offset,bytes,cbo,
            b?uint32_t(b->playing)|(uint32_t(b->loop)<<1)|(uint32_t(b->drained)<<2):0,b?b->loop_start:0,b?b->loop_bytes:0};
    }
    bool write(ReferenceCaptureRecord h,const void *bytes) {
        if(!enabled || finished)return !failed;
        if(!epoch_set || h.bytes>byte_limit-sizeof(h) || record_bytes>byte_limit-sizeof(h)-h.bytes) {failed=true;return false;}
        if(fwrite(&h,sizeof(h),1,records)!=1 || (h.bytes && fwrite(bytes,1,h.bytes,records)!=h.bytes)) {failed=true;return false;}
        record_bytes+=sizeof(h)+h.bytes;++record_count;return true;
    }
    bool flush_read() {
        if(pending.empty())return !failed;
        pending_record.bytes=(uint32_t)pending.size();bool ok=write(pending_record,pending.data());
        pending.clear();pending_voice=nullptr;return ok;
    }
    bool event(Buffer *b,uint32_t kind,uint32_t offset,const void *bytes,uint32_t count,uint64_t frame,uint32_t cbo) {
        if(!enabled || finished)return !failed;
        return flush_read() && write(header(b,kind,offset,count,frame,cbo),bytes);
    }
    bool read(Buffer *b,uint32_t offset,const void *bytes,uint32_t count,uint64_t frame,uint32_t cbo) {
        if(!enabled || finished)return !failed;
        if(pending_voice!=b || offset!=pending_offset+pending.size() || pending.size()+count>8192) {
            if(!flush_read())return false;
            pending_voice=b;pending_offset=offset;pending_record=header(b,2,offset,0,frame,cbo);
        }
        auto *p=static_cast<const uint8_t*>(bytes);pending.insert(pending.end(),p,p+count);return true;
    }
    bool emit(const float *samples) {
        if(!enabled || finished)return !failed;
        speakers.insert(speakers.end(),samples,samples+6);++output_frames;
        if(speakers.size()==48000*6 && !flush_output())return false;
        return output_frames<frame_limit || finish(true);
    }
    bool flush_output() {
        if(speakers.empty())return !failed;
        if(fwrite(speakers.data(),sizeof(float),speakers.size(),output)!=speakers.size()) {failed=true;return false;}
        speakers.clear();return true;
    }
    bool finish(bool limit=false) {
        if(!enabled || finished)return !failed;
        bool reads_ok=flush_read(),output_ok=flush_output();bool ok=reads_ok&&output_ok;
        if(records){if(fclose(records))ok=false;records=nullptr;}
        if(output){if(fclose(output))ok=false;output=nullptr;}
        failed|=!ok;finished=true;
        fprintf(stderr,"[REFERENCE CAPTURE END] engine=%llu epoch_ticks=%llu frames=%llu record_bytes=%llu records=%llu overflow_or_io_error=%u bounded_limit=%u\n",
            (unsigned long long)engine_id,(unsigned long long)epoch,(unsigned long long)output_frames,
            (unsigned long long)record_bytes,(unsigned long long)record_count,failed?1:0,limit?1:0);
        return !failed;
    }
    ~ReferenceCapture(){if(records)fclose(records);if(output)fclose(output);}
};

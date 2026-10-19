// HLE semantic transport. Raw silicon SAVE reconstruction remains guarded.
bool Nv2a::context_save_raw_range_known(uint32_t offset,size_t bytes) const {
    if(uint64_t(offset)+bytes>context_save_raw_unknown_.size())return false;
    for(size_t i=offset;i<uint64_t(offset)+bytes;++i)if(context_save_raw_unknown_[i])return false;
    return true;
}
bool Nv2a::context_save_raw_read_ready(uint32_t offset,size_t bytes) {
    if(!context_save_raw_range_known(offset,bytes)) {
        fault_("NV2A HLE context SAVE raw readback projection is unknown");return false;
    }
    return true;
}
void Nv2a::context_saved_image_written(uint32_t offset,size_t bytes) {
    const uint64_t end=uint64_t(offset)+bytes;
    saved_contexts_.erase(std::remove_if(saved_contexts_.begin(),saved_contexts_.end(),
        [&](const SavedContext& saved){const uint64_t base=uint64_t(saved.instance)<<4;
            return offset<base+Nv2aContextImage::kSourceBytes && base<end;}),saved_contexts_.end());
    for(size_t i=offset;i<end;++i)context_save_raw_unknown_[i]=0;
}
bool Nv2a::pgraph_save_context() {
#if !XLLE_RENDERER_HLE
    fault_("NV2A PGRAPH context save is not implemented");return false;
#else
    if(!pgraph_loaded_context_ || !pgraph_context_pointer_initialized_ ||
       !pgraph_context_managed_ || !(pmc_enable_&(1u<<12)) ||
       (pgraph_context_control_!=0x10010100u && pgraph_context_control_!=0x10000100u && pgraph_context_control_!=0x10000000u) || pgraph_fifo_access_ || pgraph_status() ||
       draw_faulted_ || pgraph_method_faulted_ || primitive_mode_ ||
       !vertex_indices_.empty() || !inline_array_words_.empty() ||
       !inline_vertices_.empty() || !immediate_vertices_.empty() ||
       waiting_for_flip_ || waiting_for_nop_) {
        fault_("NV2A HLE context SAVE requires an owned idle context, disabled FIFO and closed primitive");return false;
    }
    const uint64_t base=uint64_t(pgraph_context_pointer_)<<4;
    if(base+Nv2aContextImage::kSourceBytes>pramin_.size()) {
        fault_("NV2A HLE context SAVE destination exceeds instance RAM");return false;
    }
    const bool replacement=std::any_of(saved_contexts_.begin(),saved_contexts_.end(),
        [&](const SavedContext& saved){return saved.instance==pgraph_context_pointer_;});
    if(!replacement && saved_contexts_.size()>=kMaxSavedContexts) {
        fault_("NV2A HLE context SAVE capacity exhausted");return false;
    }
    auto state_bytes=[](const ContextState& state)->uint64_t {
        return sizeof(ContextState)+uint64_t(state.context_immediate_known_.size())*16+
            uint64_t(state.context_inline_known_.size())*16;
    };
    uint64_t total_bytes=state_bytes(context_state_);
    for(const auto& saved:saved_contexts_)if(saved.instance!=pgraph_context_pointer_)
        total_bytes+=state_bytes(saved.state);
    if(total_bytes>kMaxSavedContextBytes) {
        fault_("NV2A HLE context SAVE byte capacity exhausted");return false;
    }
    if(!storage_epoch_barrier() || draw_faulted_)return false;
    // Value-copy includes the LOAD image: saved/live copies never share mutable
    // ownership. No RAM, queue, clock, interrupt or telemetry enters the capsule.
    SavedContext staged{pgraph_context_pointer_,context_state_};
    auto it=std::find_if(saved_contexts_.begin(),saved_contexts_.end(),
        [&](const SavedContext& saved){return saved.instance==staged.instance;});
    if(it==saved_contexts_.end())saved_contexts_.push_back(std::move(staged));
    else *it=std::move(staged);
    // Writing a different overlapping destination invalidates its old owner.
    saved_contexts_.erase(std::remove_if(saved_contexts_.begin(),saved_contexts_.end(),
        [&](const SavedContext& saved){const uint64_t other=uint64_t(saved.instance)<<4;
            return saved.instance!=pgraph_context_pointer_ &&
                base<other+Nv2aContextImage::kSourceBytes && other<base+Nv2aContextImage::kSourceBytes;
        }),saved_contexts_.end());
    // The measured prefix extent is retained, but only independently mapped
    // surface words are serialized. Constants have unresolved SAVE anomalies.
    std::fill_n(context_save_raw_unknown_.begin()+base,Nv2aContextImage::kMeasuredSavePrefixBytes,1);
    for(size_t byte=base;byte<base+Nv2aContextImage::kMeasuredSavePrefixBytes;++byte)
        pramin_initialized_[byte/64]&=~(uint64_t(1)<<(byte%64));
    for(size_t slot=0;slot<23;++slot)if(pgraph_loaded_context_->surface_known(slot)) {
        const uint32_t value=pgraph_loaded_context_->surface_word(slot);
        const size_t offset=base+0xe0+slot*4;
        std::memcpy(pramin_.data()+offset,&value,4);
        for(size_t byte=offset;byte<offset+4;++byte) {
            context_save_raw_unknown_[byte]=0;
            pramin_initialized_[byte/64]|=uint64_t(1)<<(byte%64);
        }
    }
    pgraph_context_suspended_=true;hle_context_transport_started_=true;
    std::fprintf(stderr,"[nv2a] HLE semantic context SAVE instance=%04x; capsule preserved, unmapped raw prefix guarded, no silicon SAVE equivalence\n",pgraph_context_pointer_);
    return true;
#endif
}
bool Nv2a::pgraph_restore_context() {
#if !XLLE_RENDERER_HLE
    return false;
#else
    auto it=std::find_if(saved_contexts_.begin(),saved_contexts_.end(),
        [&](const SavedContext& saved){return saved.instance==pgraph_context_pointer_;});
    if(it==saved_contexts_.end())return false;
    if(!pgraph_context_managed_ || !pgraph_context_pointer_initialized_ || !(pmc_enable_&(1u<<12)) ||
       (pgraph_context_control_!=0x10010100u && pgraph_context_control_!=0x10000100u && pgraph_context_control_!=0x10000000u) ||
       pgraph_fifo_access_ || pgraph_status() || draw_faulted_ || pgraph_method_faulted_ ||
       primitive_mode_ || !vertex_indices_.empty() || !inline_array_words_.empty() ||
       !inline_vertices_.empty() || !immediate_vertices_.empty()) {
        fault_("NV2A HLE context restore requires idle disabled FIFO and closed primitive");return false;
    }
    if(!storage_epoch_barrier())return false;
    context_state_=it->state;
    // Eligibility is copied only with its exact saved value/known-mask owner.
    // Fresh contexts never derive eligibility from canonical numeric defaults.
    invalidate_vsh_program_cache();
    current_raster_state_sample_index_=UINT32_MAX;
    pgraph_context_user_=0x11;pgraph_context_suspended_=false;
    std::fprintf(stderr,"[nv2a] HLE semantic context restore instance=%04x; raw projection remains independently guarded\n",pgraph_context_pointer_);
    return true;
#endif
}

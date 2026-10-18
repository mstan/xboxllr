// Audited first-context initialization methods, included after the NV097 table.
// Primary attribution: context-init-method-audit-20261016 private source pins.
// Eye-vector family: xemu8fc320869718 pgraph/methods.h.inc and pgraph.c;
// NV097_SET_EYE_VECTOR has three independently addressed DWORD components.
// No method below triggers a notifier write, M2MF copy, or raster submission.
// Selectors still change owned state; nothing is accepted as an ignored method.
ContextInitializerResult Nv2a::context_initializer_method(const PushMethod& push, bool apply) {
    using Result = ContextInitializerResult;
    auto reject = [&](const char* reason) {
        char why[224];
        std::snprintf(why, sizeof(why), "NV2A context initializer ch%u/sub%u method %04x: %s",
                      push.channel, push.subchannel, push.method, reason);
        fault_(why);
        return Result::Rejected;
    };
    const uint32_t m = push.method, p = push.parameter;
    const uint8_t cls = push.subchannel < object_class_.size()
        ? object_class_[push.subchannel] : push.object_class;
    const bool notifier = (cls == 0x39 || cls == 0x97) && m == 0x180;
    const bool state_dma = cls == 0x97 && m == 0x190;
    const bool null_selector = cls == 0x9f && m >= 0x184 && m <= 0x198 && !(m & 3);
    const bool eye = cls == 0x97 && m >= 0xa50 && m <= 0xa5c && !(m & 3);
    const bool eye_vector = cls == 0x97 && m >= 0x181c && m <= 0x1824 && !(m & 3);
    const bool fog = cls == 0x97 && m >= 0x9d0 && m <= 0x9dc && !(m & 3);
    const bool color_key = cls == 0x97 && m >= 0xae0 && m <= 0xaec && !(m & 3);
    const bool texture_bump = cls == 0x97 && m >= 0x1b00 && m < 0x1c00 &&
        (m & 0x3f) >= 0x28 && !(m & 3);
    const bool named = cls == 0x97 && (m == 0x16bc || m == 0x1710 || m == 0x1e68 || m == 0x380 ||
        m == 0x2b4 || m == 0x9f8 || m == 0x147c || m == 0x1d84 || m == 0x1d7c ||
        m == 0x1e98 || m == 0x43c || m == 0x17c4);
    if (!(notifier || state_dma || null_selector || eye || eye_vector || fog || color_key || texture_bump || named))
        return Result::Unhandled;
    if (push.subchannel >= object_class_.size() || push.channel >= channels_.size())
        return reject("invalid owner index");
    if (!pgraph_loaded_context_ || !(context_objects_known_ & (1u << push.subchannel)))
        return reject("current loaded object binding is unknown");
    if (pgraph_method_faulted_ || draw_faulted_)
        return reject("previous method failed");

    if (texture_bump) {
        const unsigned stage = (m - 0x1b00) / 0x40;
        const unsigned word = ((m & 0x3f) - 0x28) / 4;
        // Envytools registers TexUnk10..15 only for stages1..3. Stage0 is
        // deliberately unqualified, not asserted to be a hardware error.
        if (!stage) return reject("stage0 bump commands are unqualified");
        // Primary matrix-command validation: sign-independent IEEE bits
        // through8.0. Scale/offset accept all32bits. The primary matrix tests
        // are marked XXX broken, so retain exact commands without inventing
        // their transformed storage, quantization, or readback projection.
        if (word < 4 && (p & 0x7fffffffu) > 0x41000000u)
            return reject("bump matrix command is outside the bounded +/-8 encoding");
        if (apply) context_initializers_.texture_bump[stage - 1][word] = {p, true};
        return Result::Ready;
    }

    if (notifier || state_dma || null_selector) {
        // Identify the current graphics object separately from its selected
        // DMA/context object. Subchannel alone is not an object owner.
        const uint32_t owner_instance = object_instance_[push.subchannel];
        if (!pramin_range_initialized(owner_instance, 4))
            return reject("graphics owner descriptor is unknown");
        uint32_t identity = 0;
        std::memcpy(&identity, pramin_.data() + owner_instance, 4);
        if ((identity & 0xfff) != cls)
            return reject("graphics owner class changed without a matching bind");
        ContextInitializerState::Object* owner = nullptr;
        ContextInitializerState::Object* free_owner = nullptr;
        for (auto& entry : context_initializers_.objects) {
            if (!entry.used) {
                if (!free_owner) free_owner = &entry;
            } else if (entry.channel == push.channel && entry.instance == owner_instance &&
                       entry.object_class == cls && entry.identity_word == identity) {
                owner = &entry;
                break;
            }
        }
        if (!owner && !free_owner) return reject("bounded graphics owner capacity exceeded");

        uint32_t instance = 0, engine = 0;
        if (!ramht_instance(push.channel, p, instance, &engine) ||
            !pramin_range_initialized(instance, 4))
            return reject("selected object has no initialized RAMHT/descriptor owner");
        ContextInitializerState::Binding binding;
        binding.handle = p;
        binding.instance = instance;
        std::memcpy(&binding.words[0], pramin_.data() + instance, 4);
        binding.words_known = 1;
        const uint32_t selected_class = binding.words[0] & 0xfff;
        if (selected_class == 0x30) {
            if (engine != 1) return reject("NULL selector has an unsupported engine");
        } else {
            if (null_selector) return reject("only explicit NULL context selectors are qualified");
            if (engine != 0 || (selected_class != 3 && selected_class != 0x3d))
                return reject("selected DMA class/engine is unsupported");
            if (!pramin_range_initialized(instance, 12))
                return reject("selected DMA descriptor bytes are unknown");
            std::memcpy(binding.words.data(), pramin_.data() + instance, 12);
            binding.words_known = 7;
            // Envytools MthdDmaNotify: Kelvin and M2MF require limit >=1F.
            if (notifier && binding.words[1] < 0x1f)
                return reject("notifier DMA descriptor is smaller than its required extent");
        }
        binding.known = true;
        if (!apply) return Result::Ready;
        if (!owner) {
            owner = free_owner;
            *owner = ContextInitializerState::Object{};
            owner->used = true;
            owner->channel = push.channel;
            owner->object_class = cls;
            owner->instance = owner_instance;
            owner->identity_word = identity;
        }
        if (notifier) owner->notifier = binding;
        else if (state_dma) owner->state_dma = binding;
        else owner->null_selectors[(m - 0x184) / 4] = binding;
        // Primary GROBJ projections are not implemented by these receipts:
        // notifier changes word1[31:16]; NV2A NULL context selectors copy
        // word0[31:8] from CTX_SWITCH_A and clear their respective enable.
        // Do not let a CPU read observe the stale input as updated hardware.
        if(notifier)owner->pramin_readback_unknown|=0xc0;
        if(null_selector)owner->pramin_readback_unknown|=0x0e;
        // Binding receipts do not claim a complete PRAMIN/cache projection.
        // Actual notify/state/copy triggers and unmapped readbacks stay guarded.
        return Result::Ready;
    }

    if (eye || fog) {
        if (apply) {
            const unsigned constant = eye ? 0x38 : 0x39;
            const unsigned lane = (m - (eye ? 0xa50 : 0x9d0)) / 4;
            // These are real Cheops aliases, not detached lighting shadows.
            std::memcpy(&transform_constants_[constant][lane], &p, sizeof(p));
            context_constants_known_[constant] |= uint8_t(1u << lane);
        }
        return Result::Ready;
    }
    if (color_key) {
        if (apply) context_initializers_.color_key[(m - 0xae0) / 4] = {p, true};
        return Result::Ready;
    }
    if (eye_vector) {
        // NV097_SET_EYE_VECTOR is three independent raw float commands, not
        // an INLINE_ARRAY alias. The admitted NONE/PROJECT2D/CUBE_MAP texture
        // modes do not consume it; constant-eye reflection remains guarded.
        // Keep every bit without inventing hardware quantization/readback.
        if (apply) context_initializers_.eye_vector[(m - 0x181c) / 4] = {p, true};
        return Result::Ready;
    }

    ContextInitializerState::Word* word = nullptr;
    switch (m) {
    case 0x16bc:
        if (p > 1) return reject("edge flag must be boolean");
        if (primitive_mode_ || !vertex_indices_.empty() || !inline_array_words_.empty() ||
            !inline_vertices_.empty() || !immediate_vertices_.empty())
            return reject("edge-flag update with active vertex history is unqualified");
        // Primary Kelvin also clears index-cache bookkeeping. At this admitted
        // idle boundary the maintained frontend has no in-flight assembly or
        // such cache; no active vertex state is discarded or fabricated.
        word = &context_initializers_.edge_flag;
        break;
    case 0x1710:
        if (p) return reject("vertex-cache break requires the zero command");
        if (primitive_mode_ || !vertex_indices_.empty() || !inline_array_words_.empty() ||
            !inline_vertices_.empty() || !immediate_vertices_.empty())
            return reject("vertex-cache break with active assembly is unqualified");
        // nxdk BREAK_VERTEX_BUFFER_CACHE; primary Kelvin Unkcf0 accepts0 and
        // clears IDX_STATE_B[15:10]. At this reached idle boundary there is no
        // assembly/cache to retain: loaded draws always refetch attributes,
        // and their transform cache is disabled (the other cache is local to
        // one batch). Preserve actual current values, formats and shader
        // decode caches; none is the invalidated vertex-buffer cache.
        word = &context_initializers_.vertex_cache_break;
        break;
    case 0x1e68: word = &context_initializers_.shadow_threshold; break;
    case 0x380:
        if (p >= 0x200) return reject("Kelvin line width encoding is unsupported");
        word = &context_initializers_.line_width;
        break;
    case 0x43c:
        if (p >= 0x200) return reject("Kelvin point size encoding is unsupported");
        word = &context_initializers_.point_size;
        break;
    case 0x2b4:
        if (p > 1) return reject("window clip type must be boolean");
        word = &context_initializers_.window_clip_type;
        break;
    case 0x9f8:
        if (p > 4 && p != 15) return reject("Kelvin opaque raster encoding is unsupported");
        word = &context_initializers_.opaque_raster_09f8;
        break;
    case 0x147c:
        if (p > 1) return reject("polygon stipple enable must be boolean");
        word = &context_initializers_.polygon_stipple;
        break;
    case 0x1d84:
        if (p & ~3u) return reject("Kelvin opaque Z configuration encoding is unsupported");
        word = &context_initializers_.opaque_z_1d84;
        break;
    case 0x1d7c:
        if (p & ~0xffff0111u) return reject("multisample control encoding is unsupported");
        word = &context_initializers_.multisample;
        break;
    case 0x1e98:
        if (p > 1) return reject("context-write enable must be boolean");
        word = &context_initializers_.context_write_enable;
        break;
    case 0x17c4:
        if (p > 1) return reject("two-sided lighting enable must be boolean");
        word = &context_initializers_.two_side_lighting;
        break;
    default: return Result::Unhandled;
    }
    if (apply) *word = {p, true};
    return Result::Ready;
}

bool Nv2a::context_initializer_pramin_read_ready(uint32_t offset,size_t bytes) {
    for(const auto& owner:context_initializers_.objects) {
        if(!owner.used)continue;
        for(unsigned byte=0;byte<8;++byte) {
            const uint64_t address=uint64_t(owner.instance)+byte;
            if((owner.pramin_readback_unknown&(1u<<byte)) &&
               address>=offset && address<uint64_t(offset)+bytes) {
                fault_("NV2A initializer PRAMIN owner projection readback is unqualified");
                return false;
            }
        }
    }
    return true;
}

void Nv2a::context_initializer_pramin_written(uint32_t offset,size_t bytes) {
    for(auto& owner:context_initializers_.objects) {
        if(!owner.used)continue;
        for(unsigned byte=0;byte<8;++byte) {
            const uint64_t address=uint64_t(owner.instance)+byte;
            if(address>=offset && address<uint64_t(offset)+bytes)
                owner.pramin_readback_unknown&=uint8_t(~(1u<<byte));
        }
    }
}

// Invoked only for the parent's separately guarded programmable, filled
// triangle/quad consumers. Point/line/fixed-lighting/shadow programs remain
// outside that profile; their stored setters are not execution authority.
bool Nv2a::context_initializer_draw_ready() {
    auto reject = [&](const char* why) { fault_(why); return false; };
    const auto& s = context_initializers_;
    if (!s.context_write_enable.known || s.context_write_enable.value)
        return reject("NV2A loaded draw context-write profile is unknown/unsupported");
    if (!s.window_clip_type.known || s.window_clip_type.value)
        return reject("NV2A loaded draw requires explicit inclusive window clipping");
    if (!s.polygon_stipple.known || s.polygon_stipple.value)
        return reject("NV2A loaded draw polygon stipple is unknown/unsupported");
    if(!s.two_side_lighting.known || s.two_side_lighting.value)
        return reject("NV2A loaded draw two-sided color selection is unknown/unsupported");
    if (!s.multisample.known || s.multisample.value != 0xffff0000u ||
        (context_raster_known_[kRasterSurfaceFormat] & 0xf000u) != 0xf000u ||
        (surface_format_ & 0xf000u))
        return reject("NV2A loaded draw multisample profile is unknown/unsupported");
    if (!s.opaque_raster_09f8.known || s.opaque_raster_09f8.value != 4)
        return reject("NV2A loaded draw opaque raster profile differs from reached value4");
    // First7 disable depth; draw8 enables LESS without depth writes. Keep the
    // existing mathematical depth/stencil operations and their storage owner.
    // Opaque1D84 is not evidence of a different comparison result. Its exact
    // early-culling mechanism remains separate from observable fragment tests.
    if ((context_raster_known_[kRasterTextureEnableMask] & 15u) != 15u)
        return reject("NV2A loaded draw texture enable selection is unknown");
    for (unsigned stage = 0; stage < 4; ++stage) {
        if (!(raster_state_[kRasterTextureEnableMask] & (1u << stage))) continue;
        if ((context_raster_known_[kRasterTextureControl0_0 + stage] & 3u) != 3u ||
            (raster_state_[kRasterTextureControl0_0 + stage] & 3u))
            return reject("NV2A loaded draw active texture color-key mode is unknown/unsupported");
    }
    return true;
}

bool Nv2a::context_initializer_clear_ready(uint32_t mask) {
    if (!mask) return true;
    if ((mask & ~0xf3u) || !context_initializers_.multisample.known ||
        context_initializers_.multisample.value != 0xffff0000u ||
        (context_raster_known_[kRasterSurfaceFormat] & 0xf000u) != 0xf000u ||
        (surface_format_ & 0xf000u)) {
        fault_("NV2A loaded clear single-sample profile is unknown/unsupported");
        return false;
    }
    // 1D84 is retained as opaque Z_CONFIG bits1..2. nxdk's older NV20 names
    // occlusion/stencil culling, but the inspected primary sources do not
    // establish a clear-pixel-value dependency (envy clear trigger is untested).
    // Do not turn raw nonzero culling state into an invented clear operation or
    // a new blanket blocker. Existing mathematical clear/storage and explicit
    // 1D80 compression/tag owners remain mandatory in the ordinary preflight.
    // Exact culling/tag optimization and changed consumers remain unresolved.
    return true;
}

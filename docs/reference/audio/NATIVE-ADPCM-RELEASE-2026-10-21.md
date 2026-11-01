# Maintained native-title ADPCM

Tracking: `beads-xbox.1.187.3`; shipping provenance remains `beads-xbox.1.21`.

Native title audio now calls the existing `xlle_apu_decode_adpcm` adapter from
`src/machine/apu_adpcm.c`. That adapter uses the unchanged, separately credited
MIT-0 dr_wav implementation. The NoRain/Phobos GPL decoder is retained only in
the explicit private comparison executable, alongside the APU decoder reference.
No new codec implementation was copied from those references.

`PILOT_ADPCM_SOURCE` is now an optional comparison input. It does not select the
production codec. Omitting it clears an older cached comparison selection, and
the native audio ADPCM contract remains available without it. Production links
the shared maintained codec; comparison notices are identified as reference-only,
and GPL comparison source bodies are not copied beside the title executable.

## Evidence

- All 512 mono/stereo reference blocks match, including every header index byte,
  nonzero reserved bytes, saturation, output extent and unused final nibble.
  Invalid arguments preserve output. Xbox consumption remains 64 frames/block.
- The selected native build passes `pilot_adpcm_reference`,
  `pilot_audio_adpcm_contract` and `pilot_audio_source_lle`; the last compares
  original/LLE source consumption with actual XAudio2 mixer output.
- An isolated JSRF native Release build loads the original Chuo save through
  the normal menus, moves and renders through a short active-audio observation
  at adaptive 1080p with FXAA/filtering. Source saves remain hash-identical.
  Background output is muted. The observer's intentional stop is recorded
  separately from a game failure.
- Its actual Release link selects `xlle_apu_adpcm.lib`; the prior GPL
  `pilot_adpcm.lib` and guest x86 interpreter are absent from that link.
- Removing the private comparison source from an existing build clears its
  cached selection and still passes the mixer/ADPCM contract checks. The title
  executable remains byte-identical to the gameplay smoke build. The shared
  driver's 42 tests pass, including the comparison-input removal contract.

An optimized local microbenchmark of 100,000 blocks per case measured:

| Decoder | Mono blocks/s | Stereo blocks/s |
| --- | ---: | ---: |
| Previous private donor | 2,325,581 | 813,008 |
| Maintained dr_wav adapter | 1,388,889 | 609,756 |

The measured added cost is about 0.29 microseconds/mono block and 0.41
microseconds/stereo block. This is a codec-cost observation, not an isolated
whole-game FPS benchmark or a hardware throughput guarantee.

This removes one incorporated GPL implementation from native title audio.
Generated CPU helpers, renderer provenance, other components, Linux support and
clean first-run packaging remain separate release requirements. It does not
clear the complete executable for distribution or advance tested game pins.

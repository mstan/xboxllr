# Native read-only display capabilities

Kernel ordinal 2 admits only the reached software capability query: null
RegisterBase, option 6, parameter zero and a valid DWORD output. It preserves the
VOID return ABI and consumes four original stdcall arguments. Mode changes and
other encoder operations remain guarded. The original GPU/LLE owner is unchanged.

The query combines validated factory AV-region and user-video EEPROM settings
with the existing native composite device policy. Its boot SMC data export uses
the same policy. SMC code 6 means composite; the encoder API's AV-pack enum uses
1 for that device. The settings first-read cache and its lock remain shared with
ExQueryNonVolatileSetting. Invalid checksums guard without writing the output.

Reference: nxdk's VOID AvSendTVEncoderOption declaration and AV-pack constants;
the retained original-kernel runner `tools/native_av_reference.py` observes
004e0101 for the matching factory/user snapshot. That runner performs software
queries only and does not qualify physical video output. The independently
authored native fixture covers that reference, preference masking, checksum
failures, cache lifetime, exact stack cleanup, EAX preservation, output canaries
and rejected encoder operations. It passes GCC and MSVC.

Actual JSRF startup now passes display discovery and reaches original device
creation at 1156 native entries. Its unbound original driver then stops on an
explicit unmapped NV2A access. Native graphics binding is the next required step;
there is no fabricated MMIO acknowledgement or interpreter fallback.

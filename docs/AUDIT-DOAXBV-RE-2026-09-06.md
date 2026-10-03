# DOAXBV native-port audit and AOT priorities

Date: 2026-09-06. Tracking: `beads-xbox.1.27`.

**Historical audit:** the source and 2/2 test results below apply to `df63e09`,
not the current release. The 2026-10-03 source refresh inspected `aa1d3c8`:
renderer/presentation and native services have expanded, and the donor now reports
playable 60 FPS sessions with private inputs. We have not reproduced those sessions.
The current policy explicitly allows compatible enhanced/HLE replacements and
requires continued assessment of all three xboxrecomp donors. See the
[updated findings and reuse candidates](RENDERER-REUSE-2026-10-02.md).

**Recommendation:** continue our portable AOT implementation. Adopt the useful
runtime boundaries and validation lessons below. Evaluate the upstream lifter
before broadening our bootstrap decoder substantially. The DOAXBV runtime is not
a drop-in renderer for our hardware model, and this audit does not justify
bundling it into xboxlle or changing the faithful default to replacement services.

## What was inspected and demonstrated

The public checkout is pinned to
[`df63e090e1a91844b73d9a53754036403a56c3f3`](https://github.com/NoRain211/doaxbv-re/tree/df63e090e1a91844b73d9a53754036403a56c3f3).
Its current [submodule declaration](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/.gitmodules)
points to sp00nznet/xboxrecomp, pinned by the Git tree to
`32da23872a552b12b4a932c9d5a6e952bb3f24bb`. This export therefore does use an
upstream submodule; it is not a locally vendored replacement lifter.

The project describes a native game port using whole-program recompilation and
handwritten service replacements. Its
[public status](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/docs/public-status.md)
does not claim that the public tests prove boot, menus, correct rendering, or
playability. Private generated game code and gameplay evidence are absent.

We built the public runtime tests with MSVC 19.41, Windows SDK 10.0.22621,
Visual Studio 2022 and CMake, in Debug. **CTest passed 2/2**:
`recomp-runtime-public-fixture` and `recomp-runtime-invalid-access`. The first
aggregates runtime, device, graphics-model, adapter, flag and SSE tests. It uses
a handwritten function fixture and the memory presenter. Neither test validates
the D3D11 renderer or whole-program translation. No private game input was used.

To repeat from that checkout with Visual Studio's C/C++ tools installed:

```powershell
cmake -S recomp-runtime -B build/public-audit -G "Visual Studio 17 2022" -A x64
cmake --build build/public-audit --config Debug
ctest --test-dir build/public-audit -C Debug --output-on-failure
```

## Rendering: substantial implementation, incomplete fidelity evidence

The current
[D3D11 presenter](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/d3d_presenter_d3d11.cpp)
contains real indexed drawing (`DrawIndexed` at line 1214), dynamic vertex/index
buffers, shaders and input layouts generated from vertex formats, transforms,
stage-0 texture sampling, depth and blend state. Its texture path includes
unswizzling and compressed formats. Describing it as only a clear/present window
would be wrong. The older Vulkan seam research note understates the current code;
we found no implemented Vulkan backend in this checkout.

The implementation also has explicit approximations. Its shader builder uses
normal direction as a stand-in color when diffuse color is absent, and its
textured path without diffuse color forces alpha to one (lines 24-119). The
[draw model](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/d3d_draw_model.c)
supports selected vertex layouts and indexed primitive types, rather than the
full Xbox graphics pipeline. These limits do not disprove the owner's observed
rendering progress; they prevent treating that progress as fidelity evidence.

The presenter caches textures by guest data address, format and dimensions
(`lookupTexture`, lines 946-1030). A hit does not check changed pixel contents.
That is a concrete invalidation risk to test before adopting a similar cache,
not evidence that this game's observed images are necessarily wrong. Cache
presence alone is also not a performance result.

The [adapter](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/d3d_draw_adapter.c)
recognizes selected guest functions and decodes title/library-specific state.
It passes a logical draw to the host renderer. Our default instead receives
guest NV2A commands and implements their hardware behavior. Connecting these
projects requires an intentional optional service-replacement path, not simply
adding another graphics backend.

## What to borrow, and what needs a different contract

| Finding | Application to xboxlle |
|---|---|
| Guest-call adapter, independently callable model, host presenter | Use this separation for future optional replacements. Keep register/stack decoding out of service models and keep host graphics details out of generated CPU code. |
| Manual function lookup in `program_manual.c` | Extend our planned guarded hook API with explicit replacement identity, profile, eligibility and decline reporting. Default-disabled hooks must preserve baseline execution. |
| Explicit synchronous buffer lifetime in `d3d_presenter.h` | Specify ownership at every handoff. Our existing NV2A DrawJob boundary already addresses part of this; an asynchronous consumer needs owned snapshots or a proven lifetime, not borrowed pointers that outlive a call. |
| Hashed generated inputs and source manifests | Keep our existing artifact identity checks. Add equivalent checks when accepting external lifter output; hashes establish identity, not semantic correctness. |
| Missing generated bodies fail loudly | Keep explicit unsupported/fallback exits. Never manufacture a successful return just to advance execution. |
| Shared XMM/x87 state across generated functions | Test full CPU state across native/fallback boundaries. The public SSE tests suggest useful cases but do not establish exact rounding, exception, NaN or denormal behavior. |
| Natural observed progress plus exact build identity | Record guest milestones, accepted/declined work and presented output separately. A call count, a passing model test, or forced progress is insufficient. |

Source contracts:
[manual lookup](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/program_manual.c),
[presenter interface](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/d3d_presenter.h),
[runtime state](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/runtime.h),
[build and input gates](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/CMakeLists.txt).

Do not adopt the CPU ABI wholesale. Its generated functions return `void`, and
its register structure lacks explicit EIP/EFLAGS. Its
[memory implementation](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/runtime.c)
exposes host lvalues and stages selected OHCI MMIO writes until another memory
access. Our adapter needs precise side-effect ordering, faults, guest deadlines,
and structured continuation exits. Its
[thread bridges](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/recomp-runtime/kernel_thread.c)
return immediately for several guest waits, which is not our scheduler contract.

There is also a snapshot-specific generated-EBP repair in CMake (lines 277-306).
This is evidence that compiling generated C is not sufficient CPU validation.
It does not prove that every upstream lifter revision retains that defect.
Qualify a pinned frontend against independent cases instead of inheriting a
game-specific source rewrite.

## Amended implementation order

1. **AOT-3 remains next.** Implement the machine adapter and guarded hook boundary.
   Include split-function extended state, ordered memory/fault/event exits,
   code alias invalidation and unsupported-hook decline cases in acceptance.
   Optional service models remain separate from call interception. No D3D8 HLE
   implementation is required to close this stage.
2. **Before substantial AOT-4 decoder growth, qualify frontend reuse.** Pin and
   inspect upstream xboxrecomp. Try a bounded synthetic caller/callee and branch
   corpus, including unsupported instructions and state crossing C functions.
   Compare emitted semantics against our independent expectations and Unicorn.
   Assess whether its lowering can target our explicit state, memory and exit
   ABI. Adopt useful compatible pieces only after that check; otherwise record
   the failing contract and proceed with a qualified decoder/lowering approach.
   A whole-program C export is not automatically a compatible machine executor.
3. **Choose later HLE experiments from our measured costs.** A guarded library
   replacement could avoid CPU work and command traffic, but must first match
   the faithful workload's observable behavior. Compare same-input interleaved
   runs, including fallback/transition cost. DOAXBV screenshots or FPS are not
   an A/B arm for our dashboard workload. No speedup is established by this audit.

This strengthens the [AOT burndown](PLAN-AOT-BURNDOWN-2026-09-06.md); it does not
close machine integration, dashboard coverage, or the 60 FPS target. Portable
AOT with manual replacements is already pursued by this project. Our
intended contribution remains a faithful machine baseline with validated,
optional replacements and modification interfaces, not a claim of uniqueness.

## License and publication boundary

DOAXBV's original runtime and documentation are
[GPL-3.0-or-later](https://github.com/NoRain211/doaxbv-re/blob/df63e090e1a91844b73d9a53754036403a56c3f3/NOTICE).
The pinned upstream lifter has a separate
[MIT license](https://github.com/sp00nznet/xboxrecomp/blob/32da23872a552b12b4a932c9d5a6e952bb3f24bb/LICENSE).
Neither license is replaced by xboxlle's PolyForm grant. No implementation,
generated game code or game assets were imported. Any later code adoption must
preserve its actual license and satisfy the existing
[combined-executable release restriction](../LICENSE-SCOPE.md).

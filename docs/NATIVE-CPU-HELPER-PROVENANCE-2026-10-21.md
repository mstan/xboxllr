# Native CPU helper provenance

Tracking: `beads-xbox.1.187.4`; complete shipping provenance remains
`beads-xbox.1.21`. This work preserves the native CPU model and its LLE contracts.

Fresh native preparations now use the attributed MIT upstream runtime template
from sp00nznet/xboxrecomp `1409a7d7801d3e931fb1074be6104209ddd9a33e`, plus a
separate authored ABI/diagnostic bridge. The unmodified upstream header, complete
license, exact hashes and source link are retained in
[the component inventory](../src/third_party/xboxrecomp_cpu/README.md).

That revision's arithmetic, floating-point and SIMD helpers match the existing
pinned fork's helper bodies. The newer upstream template also changes numerical
behavior; it is not substituted as part of a provenance change. Register/TLS
storage, guest stack arguments, original return slots, function dispatch and
native call stacks retain their existing contracts.

The authored bridge supplies current runtime declarations, original call versus
tail-call failure context, 64-bit memory access and exact RET-N diagnostics.
Normal calls remain direct native calls. Diagnostic builds also detect damaged
nonvolatile registers and both too-small and too-large stack cleanup. Missing
or non-code indirect targets still reach the runtime's explicit stop owners.
Scheduler checkpoints and original interrupt/port/publication declarations are
preserved as separately authored additions.

## Existing private preparations

The tool can upgrade a verified preparation into a new private directory:

```powershell
python tools/native_cpu_helpers.py --title ../JSRFRecomp/native-title.toml --source-generation build/jsrf-old --output build/jsrf-mit
```

It validates the title, complete generated-file inventory and preparation receipt
before copying. Only a recognized generic helper header and known authored
extensions are accepted. It does not overwrite the source or an existing target,
and it publishes the new directory only after refreshing and checking its
generation/preparation identities. Linked files/directories (including Windows
junctions) are rejected before copying or writing through them. Header/notice
updates replace directory entries, preserving hard-linked peers. Failed
candidates remain private for diagnosis.
Configure and build with the new generated directory before launching it.

Old or experimental helper variants outside the reviewed set require fresh
preparation. Original saves and owner playtest binaries are not modified.
This is a build-time change, not live HLE/LLE switching or CPU fallback.

## Qualification

The authored component checks cover header/license integrity, unchanged source
and translated bodies, preserved scheduler hooks, idempotent installation,
stale/tampered input rejection, prevention of target overwrite and preservation
of files outside the selected output. The Windows run passes seven Python checks; two real-symlink cases require
additional host privileges. Linux CI is configured to exercise those cases. Compiled
normal/diagnostic fixtures exercise resolved native calls, caller state and exact
cleanup failures, indirect call/tail-call context, non-code stops and MEM64.
Both MSVC and GCC execute those fixtures on Windows. The ROM-free Linux CI path
emits the same header and compiles/runs both variants; native Linux game-host
support remains a separate requirement.

For the private JSRF candidate, the generated inventory changes only
`recomp_types.h`: all 161 generated C files and the native dispatch inventory
remain hash-identical. Original title/menu navigation and Load Game reach the
saved Chuo gameplay checkpoint. A controller input moves the player about
27.2 world units; three further samples retain playable state over 15 seconds
of active render/audio emulation. This run uses adaptive 7:3, 1080p, FXAA, 8x
filtering and original frame timing, with speaker output muted. Captures were
reviewed; all five protected source UDATA files retain their original hashes.
The tested optimized native executable SHA256 is
`c31b0be47e621f4f2ae619139675c200db0712343c78a8f44c8d84ff3980dd2d`.
The private capture/receipt set is `cpu-helpers-20261021/jsrf-smoke-005`.
Earlier incomplete automation attempts remain recorded in Beads/private
receipts; they are not counted as passes or proof of a helper defect.

Blinx's 54 and Dashboard's 126 current generated C units use none of the
omitted fork-only LTCG/stub helper interfaces. That is an interface-use audit,
not a claim that those titles have been migrated or retested with this header.
Existing owner binaries and title pins remain unchanged. This qualification
covers the shared helper component and the representative JSRF integration;
new release builds still require their title-level package checks.

Native Linux releases are deferred by owner direction. The inexpensive
ROM-free component checks remain useful portability coverage, but do not gate
the Windows-only release target.

This replaces the copied fork header's implementation provenance. Other
compiler-emitted glue and renderer implementation provenance remain open; the
external CPU translator and retained notices are not silently relicensed.
No combined-executable distribution clearance or game-pin advancement follows
from a header replacement alone.

# Native SDK service admission

Native titles share rendering, audio and input owners. Original SDK versions
can expose different calling conventions, cache layouts or report conversions.
An SDK label or symbol-scanner name does not qualify those differences. Each
title retains exact original-image/body identities and source relationships in
its data profile; shared policies describe interfaces rather than game names.

The October 23 standup batch adds these bounded policies:

| Interface | New source-bound support | Boundary retained |
| --- | --- | --- |
| Classic D3D compact cache | Explicit 3925 map, 144 guest slots, 87 defaults, original table/initializer checks. Existing 4134 mapping and emitted header remain unchanged. | Legacy guest slot 134 is not a later semantic-state alias. Accepted Create/Reset presentation values initialize it; generic or named dynamic changes remain unsupported. Failed Reset retains its prior value. |
| Classic D3D full-index cache | Explicit 5558 map, 166 guest slots, 109 defaults, original dispatch/initializer tables and independently owned static constant storage. | Compact and full-index policies cannot be selected together. This admits a source layout; it does not implement missing graphics methods or establish gameplay. |
| Public DirectSound ABI | A public wrapper may prove one direct JMP to a unique canonical, whole-body, RET-qualified target. Both stored extents and complete decodes must agree; unproved target branches reject. | A valid thunk does not implement a method. Unsupported stream or other API names retain the existing explicit dispatch stop. |
| DirectSound object teardown guard | An exact constructor-selected vtable entry may prove one direct JMP with the same whole-body checks. | The stop remains at the original outer entry before native-object teardown; the target gains no callable wrapper. |
| Gamepad report conversion | An optional exact class-table, report-callback and activity-tail proof admits an unchanged 18-byte report. Both observed 3911 source bodies preserve light analog-button pressures. | Existing profiles retain the qualified later-SDK threshold of 32. No SDK label selects a conversion implicitly. USB transport, callback execution and six-hour console power-off are not supplied by this payload-conversion policy. |

The raw-report proof checks the full copy/activity callback topology and its
separate timer-reset tail. That tail must reference the actual retail import
table's KeSetTimer, with separate mapped timer/DPC objects. An arbitrary external
JMP cannot qualify an unfiltered report. A previously retained input wrapper can
receive this newly proved policy only when its original bodies, other layout,
receipt, manifest and old header are unchanged. The correction updates only the
report policy/header and receipt hashes; it does not retranslate CPU code.

Validation includes authored malformed-proof fixtures, both analog-pressure
policies over every byte value and all eight pressure fields, unchanged
digital/stick bytes and canaries, existing identity/4134 cache fixtures, and the
actual Reset owner under success and failure outcomes. Private original-backed
checks cover both copy-only report variants, the 3925 source tables, and the
seven observed audio thunks. Ninety retained public/auxiliary audio ABI records
from the existing three titles remain byte-equivalent, as does the existing
4134 graphics header/wrapper emission. The full-index source inspector also
accepts the private original 5558 bodies and static ranges; retained 4134 and
3925 binding output remains unchanged. The constructor-selected audio guard
passes the actual older-SDK binder while preserving retained JSRF output.

These additions do not qualify a new title's complete graphics profile or
gameplay. Air Force Delta Storm still needs its older constant/presentation
layout and Present interface admitted. Metal Arms' full-index cache and separate
original constant storage now pass source inspection; its complete graphics
binding and runtime remain unqualified.
Blood Wake's remaining service profiles are separate work. First-frame and
stage/interaction evidence stays in each title's own burndown.

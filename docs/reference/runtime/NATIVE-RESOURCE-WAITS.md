# Native resource waits

Tracking: `beads-xbox.1.137`. The source-proved `D3D_BlockOnResource`
service now uses the shared native GPU completion owner. Its canonical API
name is resolved once per thread from the validated binding table. The
implementation works with existing generated headers and requires no
title-specific enum, address or CPU regeneration.

The original 123-byte routine takes its resource in ECX and returns with
`RET 0`. The existing register adapter supplies the canonical service argument
and preserves original stack cleanup. With no device, the original routine
returns before reading the resource. With an active device, a bound resource
waits for the submitted stream; an unbound resource waits for its recorded
use. Surface views also inspect their parent's internal references.
The call preserves public references and the original fence-cache words.

Native waits now share one serial-completion implementation with ordinary
fences. D3D11 events drive the native scheduling path; the existing host-poll
fallback and original-component wait path remain available. The renderer lock
and guest CPU are handed off during a pending wait. Actual GPU completion
determines return. A completed wait leaves a bound resource bound and publicly
busy under the distinct `IsBusy` contract.

Private wait leases retain vertex, index and texture storage across that
handoff without changing the caller's public Release result. Collection runs
after the last lease leaves. Surface children retain their existing parent
ownership. Device teardown, reset and mutable GPU-alias replacement remain
guarded while affected waits are active. The current generic wait admits
known presentation surfaces, vertex/index resources and admitted texture
owners; unsupported original resource types remain guarded.

Fourteen combined Windows checks pass in 10.38 seconds. The extended runtime
fixture exercises real submitted work, texture/surface parent identity,
preserved bindings/fence words, vertex/index/presentation resources, the
production register-frame bridge, last-public-release interleaving and two
complete device lifetimes. Its CPU lookup is a test stub; actual generated
wrapper execution is established separately by the dashboard startup run.

Actual native startup passes the reached texture wait and continues creating
and drawing additional content. Its next guard is a program reading vertex
input register 7 without a declared stream element (`used=0089`, 21 slots).
That input must be resolved from the actual producer or proven irrelevant to
the consumer. Supplying an invented attribute value would not establish that
contract. Private captures show that the declaration supplies registers 0, 3
and 6 while the program reads 0, 3 and 7. The original shader constructor
copies declaration/program data without consuming vertex arrays. The next
assessment must therefore also check whether native compilation is happening
before the title establishes the actual binding. The sole captured
presentation remains black, so DB03 is active.

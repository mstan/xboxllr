# Native title driver receipts

`tools/native_title.py` accepts definitions and private path JSON outside the
framework checkout. Relative profile/seed paths resolve against the definition;
private paths resolve against their JSON file. The binding profile must select
the exact XBE SHA256. The dashboard definition selects owned retail 5960, not an
upstream dashboard substitute.

Use `prepare`, `configure`, `build`, then `run`. Preparation records the selected
profile hash, selected binding families, canonical generation-proof fingerprint
and complete generated-file hashes. Selected families must match nonempty actual
admissions in the generation manifest. Configure refuses an older
preparation under a changed profile. Configuration and build receipts bind the
definition, profile, generation manifest, generated directory, and final binary.
A manually generated/built tree has no implicit receipt adoption: run it through
the driver workflow before using driver launch. These are local consistency
checks, not signed attestations or a security boundary against an actor editing
both artifacts and receipts.

`generation.unit_max_functions` and `unit_max_bytes` bound generated compilation
units without changing function bodies. `partition` applies these budgets to a
verified existing preparation and records the transform, retaining the previous
generation/preparation receipts. It does not retranslate code or change service
proofs. Configure and build again after changing the partition. Fresh `prepare`
uses the same policy automatically; oversized single functions remain intact.

`bind` adds or refreshes reviewed whole-service bindings in a verified existing
preparation. It reruns every previously admitted family against the original
XBE and selected profile, then refreshes the preparation receipt. Removing an
existing admission requires a fresh preparation. Previous generation/preparation
receipts are retained in `binding-history`. Configure/build follow the refresh;
CPU translation is reused. A failed refresh cannot be launched as a valid new
preparation. An incomplete-bind marker blocks configuration, build and launch
even if a failed binder changed no artifacts. A successful full bind clears the
marker; a fresh preparation clears it only after all selected admissions pass.

Each launch copies the verified executable and adjacent DLLs into a unique run
stage and verifies the staged executable hash before creating the process.
Driver mutations into a marked stage or its descendants are rejected. Future
ordinary builds therefore do not replace a running owner's executable.

Owner launches are visible, have normal audio, and have no deadline. `--detach`
leaves the process open. Automated launches require `--automated --seconds N`
with a finite positive duration; they remain owned by the driver, hidden and
muted, and cannot detach. Only that created process is stopped at its deadline.
The authored tests mock process creation: they do not validate dashboard boot,
rendering, input, audio output, or native performance.

If the child exits before the deadline, the command propagates its exit status.
A planned deadline stop is recorded explicitly; it establishes only that the
bounded observation finished, not that the title passed a gameplay check.

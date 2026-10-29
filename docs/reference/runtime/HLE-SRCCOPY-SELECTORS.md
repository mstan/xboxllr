# HLE SRCCOPY optional selectors

The existing host NV09F SRCCOPY consumer does not implement chroma, clip,
pattern, ROP or beta selection. The consulted local xemu pgraph dispatch and
GL SRCCOPY consumer likewise do not consume these selector receipts.
HLE may therefore omit absent optional receipts for operation3, preserving
raw state and issuing one diagnostic. This is a feature omission, not an
assertion that hardware selected NULL or a new silicon qualification.

The current bound object must have an initialized identity matching its cached
header. Existing receipts must belong to that exact channel/instance/class
and identity; stale receipts and known nonnull selectors reject. Explicit
nonnull setters also remain rejected before copy. No NULL binding is created.
All existing DMA, format, pitch, extent, source/destination and operation checks
remain. Strict LLE still requires all six explicit NULL receipts.

Authored queued LOAD fixtures check missing-selector HLE copy bytes and later
semaphore, strict rejection without either effect, explicit nonnull rejection,
stale owner rejection and unknown operation rejection. Reopen this omission
when a title requires the omitted selector effects or a host implementation
adds an actual selector consumer.

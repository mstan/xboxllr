# Mounted content capacity

Owner: beads-xbox.1.144. Native `NtQueryVolumeInformationFile`, class 3,
supports the immutable directory-backed content provider as well as the existing
writable native HDD partitions. Content reports 2048-byte sectors, one sector
per allocation unit, and zero free units. Capacity comes from the actual mounted
directory metadata: round each file up to a sector and count one metadata unit
per directory. Empty files consume no file sectors.

This describes this provider's logical extent. A directory does not contain an
original DVD's unprovided physical layout or padding. Exact disc-image extent
remains deferred until an image provider supplies it through the same interface.

Traversal uses bounded, bulk NT metadata queries through retained directory
handles. Child directories open relative to their parent with `OBJ_DONT_REPARSE`;
reparse points and malformed metadata fail explicitly. The walk never opens file
contents or writes to the mounted media. Depth and arithmetic are bounded. Only
a complete successful traversal publishes the cached capacity; mounting a new
provider invalidates it. Failed queries preserve their real status.

The content query writes exactly the 24-byte original result and reports that
count through the IOSB. Short buffers retain the existing failure behavior and
leave both output and IOSB untouched. Writable HDD behavior stays unchanged.

Three combined media, storage and native worker/wait contract tests pass in
0.81 seconds. Fixtures cover empty, partial-sector, aligned, nested and Unicode
files, exact counts, depth limits, caching, read-only fields, short buffers and
tail canaries. Actual JSRF queries its mounted content successfully, reporting
1,222,281 sectors for the owner's directory. Startup then exposes a separate
renderer-helper initialization failure; neither a complete scene nor attract
playback is claimed by this component result.

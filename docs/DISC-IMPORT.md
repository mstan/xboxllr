# Xbox disc imports

The native launcher accepts `.iso`, `.xiso` and extracted `.xbe` inputs. For
images it reads `default.xbe` from XDVDFS and uses that executable's exact hash
to select the supported native revision. Certificate title/name diagnostics
also work inside images. A disc image does not add support for another game
or an uncompiled executable revision.

Pressing Play imports the image once, with Windows copy progress and Cancel.
The original image is read-only. Game files go under `data/discs` beside the
build; keep that directory across upgrades to reuse the import. This requires
additional space for the extracted files. Console identity, settings and native
saves remain in their existing locations and are never part of the import.

The game receives an ordinary extracted directory through its existing native
file services. Gameplay, resident preloading and streaming retain their current
implementation. Direct image mounting is not implemented. Subsequent launches
check source file identity/write time, imported file sizes and the exact XBE
hash, without rereading all disc payloads. This is a consistency check, not a
cryptographic assertion about every asset on each launch. Changing the source
image creates a distinct import; altered/incomplete caches report an error and
preserve existing data.

Imports validate directory/file extents, case collisions, Windows filenames,
node and directory cycles, overlapping records/extents and bounded traversal.
Only a complete import is published. Cancellation/errors remove the import's
own staging files; a later attempt cleans recognized crash remnants. A separate
import lease protects this cleanup from simultaneous imports. Cache metadata is
outside the game-content directory, so guest enumeration sees only disc files.
No executable media flags or other bytes are patched.

The public format reference is
[XboxDev/extract-xiso](https://github.com/XboxDev/extract-xiso/blob/master/extract-xiso.c).
The Windows progress interface is
[IProgressDialog](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nn-shlobj_core-iprogressdialog).
No extractor executable or additional Python package is required.

Focused checks use authored compact and partition-offset images, including
sparse files over 4 GiB; malformed trees and paths; unsupported games/revisions;
cache reuse; cancellation, source mutation, partial-copy failure and crash
cleanup. The portable C++ launcher reader is checked against the Python reader
and the real launcher fingerprint gate. Real-image imports and native launch
evidence remain private and must be recorded before release delivery.

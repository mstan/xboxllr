# Private inputs and publication checks

Firmware, decrypted bootloaders, EEPROMs and their extracted keys, full HDD images,
dashboard/game files, memory/network captures, credentials, and console identity
exports stay private. Keep runtime inputs in the sibling `../machine/` directory;
see [INPUTS.md](INPUTS.md). Build and play artifacts also stay outside tracked source.
Do not upload a machine folder, diagnostic bundle, or archive to an issue or release.
Logs can expose guest data, filenames, identities and local paths even when no
credential scanner reports a match.

The repository intentionally tracks source, documentation and reference hashes,
not firmware bytes or binary fixtures. `.gitignore` covers private asset formats,
archives, credential files, console metadata, generated reports and screenshots.
It leaves `src/machine/` and `goldens/*.json` visible. Ignore rules do not remove
already tracked files and can be bypassed by `git add -f`.

## Before committing or publishing

Install the repository hooks once per clone (requires Python 3):

```powershell
git config core.hooksPath .githooks
py -3 tools/check_repository_hygiene.py
py -3 tools/check_repository_hygiene.py --history
py -3 tests/test_repository_hygiene.py
```

The commit hook scans the **index bytes**, not the unstaged working copy. The push
hook scans those bytes plus every commit tree/blob reachable from local refs and
reflogs. The CI workflow repeats the tests and history scan with a full checkout.
It rejects private filenames, binary data and common credential/console-identity
patterns, printing only a path, line and category. Historical names are checked
even when a renamed file shares a blob with a harmless source path. New binary
assets require an explicit policy change and review; do not bypass a finding.

The owner requested README screenshots on October 21, 2026. Three visually
reviewed documentation images are now allowed only at the exact paths and
SHA256 values in `DOCUMENTATION_IMAGES` in the publication guard, with source
and rights information in [their notice](screenshots/NOTICE.md). This exception
only admits their binary/image format; secret-pattern checks still run. Changed
bytes, another image in the same directory, or the same binary at an unreviewed
path remain rejected, including in historical trees. It does not authorize
publishing diagnostic capture directories or original media.

Hooks are local and CI executes after a push, so neither is a guarantee against
all disclosures. Also run a dedicated secret scanner before publication, with
redacted output stored **outside** the repository. For example, with a verified
Gitleaks executable on PATH:

```powershell
gitleaks git --redact=100 --log-opts='--all --reflog --full-history' .
```

Review nonignored new files as well as tracked changes. Scan source and intended
publication artifacts, not private runtime inputs or dependency build trees.
Encoded, novel or unlabeled secrets can evade pattern matching; review remains
necessary. Reference hashes identify private inputs but cannot reconstruct them.
Do not add console serial numbers, MAC addresses, extracted keys or raw hexdumps
to otherwise safe metadata.

## Audit checkpoint, 2026-09-06

The follow-up audit at `5150e09` fetched the remote and checked its advertised
heads/tags: only `master` was advertised, with no tags or GitHub releases listed.
The all-ref/reflog hygiene scan checked **1,240 unique blobs with zero findings**;
a separate Gitleaks scan of **193 publishable working files** also found none.
No proprietary console payload was identified and no history rewrite was needed.
These checks cover the fetched Git history and inventoried working files, not
every GitHub issue attachment, external fork, cache or independently copied file.
The current source-license gap is recorded below separately.

Before this hygiene change, the local all-ref/reflog inventory covered **252
commits, 1,085 unique historical blobs and 121,883,683 uncompressed blob bytes**.
Every historical filename and full blob was checked; no private asset paths,
binary dump contents or credential-pattern findings were detected. A separate
all-object scan included dangling local objects: **2,317 objects / 1,095 blobs**,
also with no findings. It additionally searched private EEPROM content windows
without writing their values to reports.

An independently downloaded, checksum-verified **Gitleaks 8.30.1** scan of all
local refs/reflogs found no secrets. A separate scan of tracked and nonignored
working files, including pending work, also found none. Private audit scripts,
scanner binaries and redacted reports were retained outside this repository.
The remote advertised only `master` (and `HEAD` pointing to it) at audit time;
that history was included. These are scoped scan results, not a claim that a
scanner proves absence of every possible secret.

**No sensitive committed material was found, so no history rewrite was needed.**
If a later audit finds any, stop publication, revoke exposed credentials where
applicable, remove the material from every affected ref using a reviewed history
rewrite, and rescan. Deleting it in a new commit alone is insufficient. Existing
clones, forks and hosting caches need separate attention after a rewrite; follow
[GitHub's sensitive-data removal procedure](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository).

## Source licensing is a separate check

Absence of console dumps is not the same as absence of copyright. Following the
audit, the owner selected PolyForm Noncommercial 1.0.0 for his original work.
[LICENSE](../LICENSE), [NOTICE](../NOTICE), [the scope document](../LICENSE-SCOPE.md)
and [third-party attribution](../THIRD_PARTY_ATTRIBUTION.md) record that choice
and retain third-party terms. The EEPROM-generation port remains LGPL.

The release issue is still open: the current executable statically links GPL
Unicorn, and PolyForm's noncommercial restriction is not GPL-compatible.
Retaining notices alone does not fix combined distribution. No exception or
alternate Unicorn license has been obtained. Incorporated-source provenance and
transitive notices also need review before claiming a fully licensed release.

The top-level dependency files were inspected at the exact CMake pins:

| Component | Pin / source | Observed license entry point |
|---|---|---|
| Unicorn and its QEMU CPU core | Unicorn `2.1.1` | `COPYING`: GPL v2 text; also contains component-specific notices/licenses. |
| NV2A CPU VSH interpreter | `863fc0f50aa4027af113718d2a63e45ac3be7a42` | `LICENSE`: The Unlicense. |
| SDL | `release-3.2.0` | `LICENSE.txt`: zlib-style license; review bundled component notices too. |
| xemu EEPROM generator | `482088490c2d5983ed5e1eafbd09906157c1eca0` | LGPL v2 or later; source port and license text retained separately. |
| Other xemu behavioral/source references | References described in design/source comments | Upstream license/provenance must be assessed wherever code was incorporated; references alone do not establish which portions are derivatives. |

Fetched sources stay under ignored build directories; they are still part of
the build's licensing obligations. Unicorn is statically linked into the
executable. A binary distribution must address the applicable GPL source and
license requirements, plus all other included components; a link to the project
README is not a substitute for corresponding source and required notices.
This table is an inventory starting point, not a completed transitive license
audit. See [Unicorn's pinned license](https://github.com/unicorn-engine/unicorn/blob/2.1.1/COPYING)
and [GitHub's explanation of unlicensed repositories](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository).

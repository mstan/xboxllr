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

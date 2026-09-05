# Tracked golden records

Reference identities for the canonical stock-dashboard replay, carried **in the repo** so a
clean checkout can say what the emulator is supposed to produce without depending on a
`build/` tree that is ignored and machine-local.

A golden record is the JSON written by `tools/make_golden_record.py` from a replay report plus
its stdout log. It pins the final framebuffer SHA-256, the final scanout FNV-64, the 24
frame-series checkpoint hashes, the 24 per-profile output hashes, the input image SHA-256s, and
the identity of the run that produced it (exe SHA-256, git HEAD, engine, argv, chunk size,
`perf_accounting`, HDD write policy).

| file | name | status |
|---|---|---|
| `golden-noperf-defaults12-2026-09-05d.json` | `golden-defaults12-v4` | **current** |
| `golden-noperf-hddcow-92e7397e.json` | copy-on-write HDD re-baseline | historical, kept as the first record on the current disk trajectory |

## The current record

* final frame SHA-256 `36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe`
* final scanout FNV-64 `b7e78a1ac17ac5e8`
* retired 12,000,000,001 instructions, `--machine-chunk-insns 4096`, noperf, OpenGL,
  HDD attached copy-on-write
* source run `build/golden-ref-v6.report.json`, exe SHA-256 `054f6585…`

`exe_sha256` is a BUILD identity, not a content identity: the mingw linker stamps the link time
into the PE header, so relinking an unchanged tree produces a different SHA (`054f6585` and
`6fd3714f` are the same sources). Settle identity questions on behaviour and on the recorded
argv, not on the exe hash alone.

## Which configuration reproduces which part of it

The record is taken in the **reference configuration**, `--nv2a-draw-threads 0
--nv2a-profile-hashes`, in which the GPU executor takes zero jobs (every job is rejected
`reason=hash_on`). That is deliberate: it makes the record the software-raster reference the
shipping defaults have to reproduce, rather than a snapshot of one accelerated path.

* **Final SHA and scanout FNV**: reproduced by the shipping defaults as well — every arm of
  every measured set on 2026-09-05 ends on `36e87a97…`.
* **The 24 per-profile hashes**: only emitted with `--nv2a-profile-hashes`, which is off by
  default (`docs/PERF-METHODOLOGY.md` §3b). Compare them on reference legs only.
* **The 28 checkpoint PPMs**: 24 are byte-identical under the shipping defaults; 4 differ by a
  handful of single pixels whose mechanism is measured in `docs/PERF-METHODOLOGY.md` §3a.
  Acceptance is behavioural, not byte-exact (§3).

## How to use one

```powershell
# gate a reference leg on the whole record
.\tools\run_stockdash_replay.ps1 -Label ref -GoldenRecord goldens\golden-noperf-defaults12-2026-09-05d.json `
    -ExtraXlleArgs @('--nv2a-draw-threads','0','--nv2a-profile-hashes')

# gate a default-configuration leg on the final SHA (the harness default)
.\tools\run_stockdash_replay.ps1 -Label defaults
```

## How to make a new one

```powershell
python tools\make_golden_record.py --report build\<label>.report.json `
    --stdout build\<label>.stdout.log --name <name> --out goldens\<file>.json
```

Re-pin whenever a faithful change moves the record (`docs/PERF-METHODOLOGY.md` §3: no owner
sign-off needed), and record the evidence in §3a of that file. The per-run evidence itself —
reports, PPMs, logs — stays under `build/`, which is ignored.

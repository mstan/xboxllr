# OpenCode Ox Alpha long-run handoff (for a fresh model)

Date context: 2026-08-21.

This document is a zero-prior-context prompt for a continuous bring-up/research run.
It is written to keep work faithful to project doctrine, measurable, and recoverable.

## Read this first (in order)

1. `AGENTS.md` from the repo root context provided by the user (`xboxlle-probe/AGENTS.md` for hardware safety and tooling behavior).
2. `xboxlle/PRINCIPLES.md` and `xboxlle/PROGRESS.md`.
3. `xboxlle/STATUS.md` (newest entry first).
4. `xboxlle/NORTH-STAR.md`.
5. `xboxlle/docs/ROADMAP.md`, then `xboxlle/docs/DESIGN.md`, then `xboxlle/docs/INPUTS.md`.
6. `xboxlle/PROGRESS.md` again to align current burndown priorities.

## Hard non-negotiables

- Non-shortcut doctrine: LLE-first, no fake success, no stubs, fault-stop loud on unknown behavior.
- HLE is allowed only as an opt-in acceleration after a faithful LLE path exists.
- No correctness compromises for speed.
- No copyrighted bits in git (`machine/` contents remain git-ignored/never committed).
- No direct subagent work on live hardware unless explicitly authorized by the human.
- Never scan LAN or infer Xbox IP.
- For hardware probes, require explicit target/authorization each time.

## Current state snapshot

As of `2026-08-21`, Arc 1 / A4 is active and the blocker is interactive speed plus input correctness.

- Stock dashboard boots through LLE and produces scanout.
- Best retained exact path is still under `XLLE_TEXTURE_COMPUTE_TILE_BINS=1`.
- Project hard goal remains at least 10 presented fps for stock dashboard navigation in `NORTH-STAR.md`.
- User stretch goal for this run: push toward 60 fps with sustained headroom while preserving LLE-first correctness.

## Branch policy (mandatory)

- Work only on branch `opencode/ox-alpha-20260821` (this branch).
- Do not push to `master`.
- Commit on meaningful milestones only.
- Update `xboxlle/STATUS.md` before each milestone commit.
- Keep working tree clean outside staged milestone changes.

## Beads workflow

Treat this as substantive engineering work and track through Beads.

- Do not initialize per-repo Beads metadata.
- Use central issue store: `F:\\Software\\beads\\issues` (`mstan/beads-issues`).
- Confirm/update the owning issue. Docs currently reference `beads-xbox.1.13`.
- If no active issue exists, create: `beads-xbox.<next-seq>` under the hierarchy:
  - `scope:system` with a value matching the project's existing Xbox system label,
  - `scope:game` with `game:Xbox` or equivalent repo-specific game/meta label,
  - repo label: `repo:xboxlle`.
- Record plan, findings, and milestone commits in Beads, and close only after validation.

## A|B measurement protocol (must be used for optimization candidates)

- A-path: retained production-like path (no perf-accounting).
- B-path: same run with perf accounting enabled (`--perf-accounting` or current equivalent).
- Required comparisons:
  - Replay hash equality (must be exact unless explicitly gated and justified).
  - Final and final-four wall-time deltas.
  - Presented FPS delta.
  - Visual diff evidence via fixed-frame capture points.
- Any runtime speed win with hash drift must be documented and not promoted until rerun through hardware-oracle constraints.

## Work loop

1. Read `STATUS.md` newest entry and latest `PROGRESS.md` milestones.
2. Choose one bounded optimization or correctness fix target.
3. Build and run 1B smoke + 12B bounded replay on both A and B paths.
4. Keep outputs in `build/` and cite exact artifacts.
5. If visual regressions exist, treat them as blockers ahead of throughput claims.
6. Update `STATUS.md` and `PROGRESS.md` with evidence references.
7. Commit with concise, date-stamped message.

## Priority order for this run

- Preserve exactness while fixing visual errors in dashboard render path.
- Finish Controller-S path evidence: model hub/topology + real endpoint-2 polling proof.
- Move heavy dashboard work toward `60 FPS` envelope only if exact path remains intact.
- Preserve and extend observability rather than bypassing behavior.

## Hardware probe constraints (when used)

- Use read-only probes first: `ping`, `info`, `probe cpu`, `probe nv2a`.
- Keep all state-changing operations off by default.
- Follow `xboxlle-probe/AGENTS.md` and `xboxlle-probe/README.md`/`SECURITY.md` for any dangerous command.

## Exit criteria for this session prompt

- A/B protocol executed on each candidate optimization.
- No unplanned regression in exact replay hashes for retained path.
- Visual defects list reduced and evidence captured.
- Updated milestone notes in `STATUS.md` and `PROGRESS.md`.
- Commit set captures a safe handoff point for continuation or rollback.

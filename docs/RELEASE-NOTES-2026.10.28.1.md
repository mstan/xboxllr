# Native architecture update ? 2026.10.28.1

The owner-accepted package gives native effects responsibility for draw submission/preparation, centralizes native mapping commitment/logical access, and reuses immutable pipeline states across material changes. It preserves required GPU completion/callback behavior, original device ownership and portable fallbacks.

Matched Blinx Stage2-1 NAS observations show paired medians of about 15% less CPU per original update and 27% greater original-update throughput. Busy-host conditions limit exact magnitudes; these are not universal game/host guarantees. Both JSRF revisions are conservatively neutral/inconclusive overall, with lower CPU observations and capped/mixed source-swap throughput. See [full per-game evidence and 27 dispositions](optimization/2026-10-28/README.md).

Current native saves, gameplay, time-control and area-loading checks passed within the documented scope. The exact frozen baseline reproduces the existing allocator-ABI fixture failure; that check remains explicitly failed/shared. Native saves/settings remain protected. No save-state backward compatibility is promised.

# xlle-agent

A resident control/probe agent for the **real Xbox** — the hardware-oracle tier
(PRINCIPLES §8, DESIGN §8). Once running on the console it hosts a TCP command server so the
PC-side driver (`tools/xbox_agent.py`) can read/write guest memory, push and execute
arbitrary payloads, launch XBEs, and run hardware probes — feeding the cosim / hardware-
conformance harness. It is **non-destructive**: it never writes storage, and `DASH` returns
the user to their dashboard.

## Protocol (tcp/4380, one client at a time, line-based ASCII)

| Command | Reply |
|---|---|
| `PING` | `PONG <ver>` |
| `INFO` | one JSON line: kernel version, tick count, port |
| `R <hexaddr> <len>` | `OK <hexbytes>` (len ≤ 4096; direct read) |
| `W <hexaddr> <hexbytes>` | `OK <n>` (direct write) |
| `EXEC <len>\n<raw>` | `RET <hex>` (copy raw, call `int(*)(void)`) |
| `L <path>` | launches an XBE (no reply on success) |
| `DASH` | returns to `C:\evoxdash.xbe` |
| `QUIT` | close connection (agent keeps listening) |

## Build (WSL Ubuntu; nxdk toolchain)

```sh
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu -- bash /mnt/f/Projects/xboxlle/build-nxdk.sh \
  /mnt/f/Projects/xboxlle/xboxlle/agent
```
Produces `default.xbe`. (`build-nxdk.sh` sets a clean PATH and symlinks Ubuntu's `llvm-*-18`
tools to the unversioned names nxdk expects — no sudo.)

## Deploy + drive

```sh
python tools/xbox_agent.py deploy agent/default.xbe   # FTP upload (+ best-effort HTTP launch)
# if HTTP auto-launch is unavailable, launch E:\Apps\xlle-agent once from the dashboard
python tools/xbox_agent.py ping
python tools/xbox_agent.py info
python tools/xbox_agent.py read 0x80010000 64
```

## Status

v0.1 — first version. Memory read/write is direct (an unmapped address can fault the agent;
fault-guarding is a later hardening). x87/SSE and structured probe commands (feeding the
JSONL rings) come next.

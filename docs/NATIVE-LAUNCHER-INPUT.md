# Shared launcher artwork and controller source

The native Xbox launcher uses recomp-ui's Player 1 card, SDL device names,
GUIDs and live hot-plug list. The host's bounded catalog/result handoff carries
the chosen None/Keyboard/Gamepad source and GUID. Python validates and persists
that identity transactionally alongside the independent mod choices. First use
selects a connected gamepad or Keyboard; a saved gamepad remains selected when
disconnected. The Xbox card reports actual GUID presence rather than treating
an assigned but absent pad as connected. Configuration exposes source selection;
unconsumed remap/deadzone controls remain hidden.

`PILOT_SDL2_DIR` optionally enables the SDL2 host transport using the same SDK
as the launcher. One worker owns SDL event pumping and controller handles.
Before initializing SDL, it enables `SDL_HINT_JOYSTICK_THREAD` so SDL's Windows
notification window has its own Win32 message pump. The native renderer does
not initialize SDL video; `SDL_PumpEvents` alone does not dispatch the Windows
RawInput messages in that configuration. Without the notification pump, a pad
can enumerate and open successfully while every sampled button/axis stays zero.
It publishes locked Xbox-format snapshots, uses the existing face/Black/White
mapping, converts down-positive Y and signed triggers to the existing Xbox
report, and queues rumble to the handle owner. Only guest port 0 is assigned.
A disconnected selected GUID reports device-not-connected and cannot silently
fall back to another attached controller. Reconnect scans run on the worker,
not guest CPU dispatch. Models with identical GUIDs use the first match; this
does not claim a serial-number identity contract.

The original XInput/keyboard/replay/control transports retain their behavior.
Without the optional SDK, the SDL source compiles without SDL headers/imports;
explicit SDL selection is rejected as unsupported. No guest ABI, USB report,
native CPU conversion or LLE qualification changes are needed.

Titles supply artwork through `XLLE_LAUNCHER_BOXART`, staged through recomp-ui's
existing `BOXART` seam. `XLLE_LAUNCHER_ARTWORK_NOTICE` stages title attribution.
The generic framework embeds no Blinx artwork or guest addresses.

Validation includes actual SDL virtual devices for mapping, signed axis and
trigger endpoints, wrong-device isolation, disconnect/reconnect and cleanup;
the existing input/replay/stop contracts; compiled launcher/provider selection
round-trips and malformed-state rejection; and the no-SDL portable compile.
Physical device feel and hot-plug remain useful owner playtests.

The headless Windows event-pump test targets only the current process's SDL
message-only window with `WM_NULL`, checks dispatch while the caller is idle,
and verifies shutdown removes the window. It needs no physical controller and
fails with a timeout on the original unpumped transport. This covers a boundary
that SDL virtual joystick samples do not exercise. An Xbox One physical capture
also compared simultaneous default, manually pumped, and SDL-threaded samples:
the threaded path received A/Menu and full-range sticks with the original GUID,
while the default path queued hundreds of unprocessed RawInput messages.

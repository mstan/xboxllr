# Owned offline virtual-link status

Tracking: `beads-xbox.1.137`. The native dashboard reached its link-status
wrapper after creating the three ambient audio streams and opening its font.
The source caller uses the returned active bit. The shared implementation owns
an explicit **unattached virtual adapter** for the OFFLINE profile; desktop
Ethernet/Wi-Fi and the physical Xbox are separate devices.

The unchanged original wrapper returns the complete raw 32-bit PHY state when
bit zero is active, otherwise zero. It queries with argument zero, preserves
nonvolatile registers and has RET0. It temporarily raises/restores IRQL and
initializes the original PHY only when both its network owner and the current
thread's initialized field are absent. Those private original initialization
details are replaced by the native backend's explicit virtual-device policy;
no fabricated original object pointer, PHY-register write or successful raw
kernel PHY stub is installed.

The portable core queries a provider under the existing lifecycle lease. It
allows queries before online startup and after final cleanup, as the original
wrapper does, while rejecting inconsistent owner/reference state. It preserves
all active raw flag bits, including high bits, and never converts them to an
HRESULT or sets guest LastError. A missing or failed provider remains an
explicit guard. The host provider admits only the selected unattached policy;
an unknown policy does not silently become a disconnected result. No socket,
receiver, host-NIC poll or network traffic is introduced.

The optional exact-image binding proves the wrapper body, RET0, network-owner
reference, thread-context operand roles, four kernel imports and order, and the
returned flag-mask sequence. Addresses, offsets and value-register selection
live in the title profile. Older headers and profiles without the link feature
retain their previous behavior. Retained lifecycle/cache/random bindings may
upgrade only when all earlier bodies, metadata and wrapper bytes match; the
new header records the virtual attachment selection. All other private network
consumers remain guarded.

Evidence so far:

- 64 unchanged-original wrapper cases pass with authored owner/thread/IRQL
  context and hooked lower-level import boundaries. They cover mask results,
  lazy-initialization conditions, arguments, IRQL restoration, RET0 and
  nonvolatile registers. This is a wrapper component proof, not physical PHY,
  NIC boot or real cable negotiation qualification.
- The portable core passes GCC `-std=c11 -O2 -Wall -Wextra -Werror` checks.
  It tests raw flag results across pre-startup,
  two owned references and cleanup, failure precedence and lifetime balance.
- All 15 authored network binding tests pass. Actual retained binding changes
  only `pilot_network_bindings.h` among 119 generated files; original CPU and
  wrapper bytes stay unchanged, and repeat binding is idempotent. Five public
  operations are admitted; 78 other consumers stay guarded.

The Windows production-dispatcher fixture also builds with MSVC `/W4 /WX` and
passes RET0, raw flag, register/guest-memory preservation, no-LastError and
provider-failure checks. The real host fixture confirms the explicit unattached
policy before/during/after real Winsock ownership and rejects missing/unknown
policy without changing output. All ten combined network, worker, scheduler and
audio-notification checks pass in 5.22 seconds. All 103 generated CPU objects
are reused. Host disk contention made configuration/build time variable; cache
warming is a development experiment, not a qualified speedup claim.

Actual native executable
`20fe92482a6e1a564ec0852c31e58d2b5240ccc94674a4d5ff39fb838d131c48`
advances past the previous link-status boundary into further dashboard/audio
initialization. It stops explicitly at `IDirectSoundBuffer_Pause` (audio table
entry 8), after 222,225 entries on the stopped thread, with child exit 2 before
the 20-second deadline and no captured frame. Entry count is not a unique
coverage percentage. The method is already hash/ABI-bound; its native buffer
state transition is the next implementation gap.

Physical PHY/link behavior remains a separate
[deferral](../runtime/NATIVE-TITLE-DEFERRALS.md); a menu frame, controller, audible audio,
performance and Credits acceptance still require actual title evidence.

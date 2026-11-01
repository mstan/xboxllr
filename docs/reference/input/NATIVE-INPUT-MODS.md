# Native camera input mods

`xboxlle.enhancement.camera` exposes two independent, default-off checkboxes:
**Invert horizontal camera** and **Invert vertical camera**. They apply to
Player 1's right stick at the common host-controller boundary, including SDL,
XInput, keyboard, TCP and authored replay sources. Other ports, movement,
buttons, trigger values, capability bitmasks and disconnected reports retain
their original behavior. The original Xbox input model and title code are
unchanged. These are input preferences, not a higher-rate simulation profile.

The launcher persists each checkbox through the existing mod-selection file.
The shared release and developer launchers translate them into startup-only
`XLLE_PILOT_CAMERA_INVERT_X` / `XLLE_PILOT_CAMERA_INVERT_Y` values (`0` or `1`).
Invalid values stop explicitly. Packages contain data and select the compiled
input implementation; they cannot supply code. No graphics mod or widened
camera is required to invert input.

The packed Xbox report remains little endian on every host. Inversion keeps
zero neutral and maps -32768 to +32767 without signed overflow. Ordinary
diagnostic capture records the resulting report. Strict diagnostic replay
delivers that recorded report directly, so inversion is never applied twice.
The session retains the original mod selection and executable identity.

## Focused validation

- `pilot_input_camera_mods`: every signed 16-bit value, both axes independently
  and together, including unchanged movement/buttons and neutral/endpoints.
- `pilot_input_camera_replay`: real `input_host.c`, deterministic control
  transport, capture/replay in all four combinations, unchanged capabilities,
  rejected secondary port and output preservation on disconnect.
- `pilot_input_sdl_host_transport` and `pilot_input_sdl_camera_x/y/xy`: actual
  SDL virtual controller transport, selected-device isolation, signed axes,
  triggers, disconnect and reconnect.
- `tests/test_native_input_mods.py`: checkbox-only catalog, independent saved
  selections, startup policy and compiled launcher round trip.

These checks establish mapping and lifecycle behavior. A player's preferred
camera direction remains a normal playtest choice.

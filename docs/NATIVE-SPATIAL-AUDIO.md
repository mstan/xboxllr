# Native spatial audio burndown

Tracking: beads-xbox.1.152. Active spatial output is still required work.

The optional `spatial-cache-v1` profile binds thirteen original public setters
through their exact buffer and voice helper bodies, including original callee
cleanup and call edges. Older profiles acquire no spatial admission. Parameter
layout follows the reviewed original helpers and agrees with Cxbx-Reloaded
`X_DS3DBUFFER` at `585c49a50af1255ab155099e06f24505f9c5a800`.

The shared owner retains 18 explicit buffer parameters and nine I3DL2 source
parameters, original dirty masks, apply's low bit and device-wide deferred
commit. SetAllParameters ignores its size word as the original does. Unknown
defaults remain unknown; zero-initialized storage is not claimed as an original
default. Mono spatial buffers can own inactive PCM/ADPCM storage. Notification
capability can be requested without importing a notification setter or
registering any events; no event completion is invented.

Spatial Play still stops before submitting sound. Cache commits also reject
active output. The ordinary 2D audio/listener regression and actual public
13-setter ABI/cache/lifetime fixture pass in 0.53 seconds. The exact active
Play guard is verified separately. Three source-admission suites reject changed
bodies, cleanup, incomplete families and mismatched public/buffer/voice chains.
These checks do not establish spatial sound, HRTF or complete attract behavior.

The next output phase can reuse the original 16-function, 1900-byte closed
spatial arithmetic graph: relative position, original angular approximation,
distance/cone attenuation, Doppler and HRTF table selection. Private inspection
also identifies 31 signed coefficient bytes per ear and the original integer
interaural delay selection. Original coefficient tables stay in user-supplied
inputs. No game code or coefficient table is shipped here.

The output owner must preserve mono source-byte positions and notifications
while producing stereo HRTF output and actual effect sends. XAudio2 locks effect
output channel count when a voice is created, so a mono-to-stereo effect must
be supplied at creation rather than attached later. See the official
[SetEffectChain contract](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-seteffectchain).
The retained MCPX contract and xemu `hw/xbox/mcpx/apu/vp/` at
`482088490c2d5983ed5e1eafbd09906157c1eca0` provide filter/routing reference.
Exact hardware transition smoothing/headroom remain explicit questions for
that implementation; no hardware comparison has been made.

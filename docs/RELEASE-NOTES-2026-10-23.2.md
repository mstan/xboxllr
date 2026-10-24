# Windows controller hotfix - October 23, 2026

This update fixes controllers that appeared connected in the launcher but gave
no input in a native title. The shared SDL2 transport now enables SDL's Windows
joystick notification thread before initialization. The native renderer uses
its own window rather than SDL video; without that notification thread, Windows
RawInput messages could remain queued while buttons and sticks read as zero.

The correction is compiled into Blinx, Jet Set Radio Future and Xbox Dashboard.
It keeps the same selected controller and normal hot-plug behavior. No Steam
setting, Steam shutdown or machine-wide environment change is required.

Blinx retains the damaged-enemy health-circle and camera/presentation fixes.
JSRF retains its default-on Early Test Runs mod and area graffiti-soul
requirements. Dashboard retains native startup in its game window and the
console-free Start Game.exe launcher. Gameplay timing and each title's other
documented limitations remain unchanged.

Preserve the existing package's data folder, selected save storage and console
identity when updating. Native in-game progress is separate from disposable
build-specific save states. The clean release packages contain no personal
saves, original game assets or console signing inputs. Matching project and
prepared dependency source is included locally under source/.

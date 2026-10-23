# Xbox Dashboard Windows preview — October 23, 2026, update 1

This Dashboard-only update preserves one gameplay window from the full Xbox
startup animation into the original Dashboard. Its position and size survive
the transition, menu input remains responsive, and normal window closing works.

Open **Start Game.exe** after extracting the Windows ZIP. Normal launches no
longer show a developer console; diagnostic logs remain available. Full startup
defaults on for Dashboard. **Mods → Xbox Startup → Skip startup animation**
goes directly to the menu and is off by default. The launcher uses the selected
Xbox logo artwork and does not expose Blinx's camera-inversion options.

Supply your own matching Dashboard 5960 executable and complete asset folder.
Startup needs data prepared locally from a compatible complete kernel dump;
missing or unsupported startup data launches the Dashboard directly. Original
Dashboard files, firmware, boot assets, EEPROMs, disk images and personal saves
are excluded from the download. Keep your existing `data` folder and external
save storage when upgrading.

The native startup presentation is not full firmware execution. Supporting
physical audio precision remains documented separately. Matching project and
prepared dependency sources and notices accompany this GPL preview. Linux
binaries remain deferred.

The owner accepted the presentation. A muted native check reached the real
Dashboard, navigated its menu in the same resized window and closed normally.
All four native startup fixtures passed; Python regression checks passed 61
tests with one additional private-input skip. This refresh leaves Blinx and
JSRF releases and framework pins unchanged.

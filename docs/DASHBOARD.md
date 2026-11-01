# Xbox Dashboard beta

The Dashboard download is a native Windows application, launched like the game
packages through **Start Game.exe**. It is part of the same
[XboxLLR beta release](https://github.com/mstan/xboxllr/releases/tag/beta-2026.10.29.2)
as the separate developer build tool. It requires your own Dashboard 5960
executable and complete asset tree. See [setup](SETUP.md#dashboard-beta-quick-start).

The [README support table](../README.md#dashboard-support) is the current public
summary. Dashboard 5960 is the admitted revision; a matching name or title ID
does not admit a different executable.

## Tested behavior

The native profile has exercised startup/Home, controller navigation, Music,
Memory and Settings opening/Back, and normal close. User settings have covered
Normal/Letterbox/Wide Screen, language, Mono/Stereo/Dolby Surround, timezone/DST,
parental ratings and passcodes. A fresh process accepted a saved parental
passcode. Credits scrolling/section navigation and ordinary System Info were
also exercised. The latest packaged check used a newly generated virtual console
and spot-checked Home, Settings and Back. These checks are bounded and do not
establish every submenu/device combination.

Settings writes preserve the supplied EEPROM and its protected sections. A
validated, source-bound user record is stored in the native state directory.
Keep the package's `data` folder and selected external save/HDD profile across
updates. A generated local identity differs from your physical console's identity.

Time/Date Apply changes a console-session clock without setting the host clock.
Persistent RTC behavior across restarts and the six-hour Auto Off action remain
unverified. Audio setting selection is distinct from proving output fidelity.

## Partial and unavailable features

Memory and Music navigation work, but complete save-copy/delete, soundtrack,
CD/ripping and memory-unit workflows have not been qualified. The package uses
an empty optical-drive policy and does not expose physical optical/USB memory
devices. Existing raw/QCOW2 disk import has identity/layout checks; it is not a
claim of complete Dashboard storage-management parity.

Network Settings and Auto Sign-In use an offline notice with working Back.
Original XoDash network setup, Xbox Live and connected-network behavior are
unsupported. The native Dashboard cannot execute arbitrary game XBEs. Full
firmware boot is separate from the optional reconstructed startup animation.
Only the Windows x64/D3D11 app is released. Short menu-entry hitches and graphics
precision questions remain open.

## Screenshots and developer references

The README shows real native TCP captures of Home and Settings. PNG export
discards the swapchain's ignored alpha and preserves RGB exactly. The former
exports incorrectly used framebuffer alpha as PNG transparency, which caused
washed-out images on white pages; that was a capture-export error.

Deeper implementation and test limits are in
[settings](reference/dashboard/NATIVE-DASHBOARD-SETTINGS.md),
[materials](reference/dashboard/NATIVE-DASHBOARD-MATERIALS-2026-10-20.md) and
the [reference index](reference/README.md). Historical task reports remain in
Git history. Please [file a GitHub issue](https://github.com/mstan/xboxllr/issues)
with the release, menu/action and reproducible steps; do not attach console keys
or original Xbox content.

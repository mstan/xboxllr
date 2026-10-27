# Graffiti tools, diagnostics and renderer corrections — 2026.10.26.3

This toolkit update supplies the shared framework used by the accompanying
JSRF Windows preview. The toolkit contains source/Python tools and exact
prepared dependency source, rather than a standalone compiler executable.

- A native crash-report window explains the recorded stop and can copy a
  shareable diagnostic ZIP. Console identity, keys and native saves stay local.
- The launcher supports a title-owned image manager with asynchronous import,
  saved-design thumbnails, full-size preview, selected deletion and automatic
  capacity refresh. Folder and FATX storage use the same exclusive writer path.
- Content-bound high-resolution graffiti replacements retain the game's native
  dimensions, UVs and readback, and reject stale companions after native edits.
- Fixed-function texture-coordinate transforms now retain the homogeneous
  component required by the original portrait/name atlases. Fixed and programmed
  geometry share the original viewport/sample-grid treatment. Rectangle clears,
  texture formats and native graphics allocation lifetimes receive corrections.
- The opt-in localhost QA interface supports virtual Player1 presence and
  isolated, expiring input on four ports. Diagnostic submitted-draw capture is
  bounded and preserves actual original program provenance outside source kits.

Validation includes shared native/GPU contracts, both optimized JSRF revision
builds, actual Roboy graffiti and multiplayer roster checks, owner-confirmed
custom graffiti in gameplay, and the JSRF scripted-intro skip comparison. See
the JSRF release notes for that title's optional hold-Start mod. Exact
Dogenzaka corner-flicker and earned Test Run/post-run preview checks remain
separate pending playtests; this release does not claim a full campaign audit.

Keep the complete existing `data` folder when updating a game. It contains
native saves, console identity, settings and imported-disc caches. Only RPTK
and RAID Discord source emblems accompany JSRF; personal imports are excluded.
The existing Blinx and Dashboard playable archives are unchanged.

# XboxLLR public beta — 2026.10.29.1

XboxLLR is an early low-level original Xbox recompiler ecosystem. Blinx is the
only publicly supported game. Compatibility and hardware coverage are incomplete;
please report reproducible bugs through GitHub issues.

Two separate XboxLLR releases are provided: the playable native Dashboard 5960
package and the developer build toolkit containing xboxllr.exe, bundled Python,
Capstone and exact framework source. Blinx has its own playable public alpha.
Windows x64/D3D11 is the playable target. No native Linux game release is claimed.

Dashboard needs your own matching xboxdash.xbe and full C-partition asset tree.
Start Game.exe opens the optional console setup; Skip creates a new local profile.
The optional native startup animation needs compatible complete kernel data;
otherwise the Dashboard launches directly. Xbox Live remains offline. See
docs/SETUP.md. Game media, firmware, keys and personal state are excluded.

Shared storage now offers sparse RAM FATX utility volumes. Blinx enables its
750 MiB disposable cache by default; persistent saves retain disk storage.
Design inspiration: apexJCL/blinx1-recomp. No upstream code bodies were copied.
Release scripts are tools/release_dashboard.py and tools/release_toolkit.py.

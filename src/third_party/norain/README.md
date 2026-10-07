# NoRain texture layout implementation

Source: https://github.com/NoRain211/doaxbv-re
Revision: `aa1d3c8afcd8d063171d819e27307070b63f056b`.

`d3d_texture_model.c` and `.h` are unmodified files from `recomp-runtime/`.
They are GPL-3.0; see `LICENSES/GPL-3.0.txt` in this repository.
The separate HLE renderer uses their rectangular texture unswizzling.
The donor's game-specific addresses, draw-decline policies, and CPU hooks are
not dependencies of this module.

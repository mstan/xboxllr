# Native packed movie uploads

`PILOT_GPU_MOVIE_UPLOAD=ON` enables the Phobos D3D11 packed YUY2/UYVY upload
path. The framework default is OFF. In an enabled build,
`XLLE_PILOT_GPU_MOVIE_UPLOAD=0` selects the original CPU conversion at startup.
Other backends keep their existing implementation.

The path retains raw CPU texture bytes and the existing changed-frame,
batch-flush, content-version, replacement and completion publication. It
uploads two packed bytes per pixel, converts on the GPU using exact integer
BT.601, then copies the result into the existing native-sized sampled image.
Its private packed/converted images do not scale with presentation resolution.
The title's original decoder, audio and playback controls remain responsible
for the movie.

Active guest visibility queries, unsupported dimensions/formats/layouts and
unavailable GPU resources use the original CPU path. Private GPU resources are
released on close/reset. This optimization does not require a render-target
view on the guest image and preserves the graphics state touched by the pass.

The existing graphics fixture checks padded pitches, both byte orders,
successive frames, retained raw ownership, sampled RGB pixels and query
fallback. `pilot_graphics_test --movie-color-space` additionally checks all
256 cubed input triples in both byte orders against independent integer
equations and asserts actual GPU use. Run the ordinary fixture with the GPU
selector both enabled and disabled. Component equality does not qualify a
title's playback timing or audio; validate those on the built title.

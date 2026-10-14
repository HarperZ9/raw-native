# From snapshot 2 to snapshot 3

Date: 2026-10-14. Cause: screen-space reflections march one step a pixel to the screen edge and end
the march when the ray enters a solid through a face the camera cannot see, using a back layer
(method note 14 of 2026-10-14 in `evidence/m3-post-bounds.json`). The rasterizer also clips
triangles at the near plane instead of dropping them (method note 13); that changes output, not
declarations.

## What changed

| Declaration | Snapshot 2 | Snapshot 3 |
|---|---|---|
| `SsrParams::maxDistance` | `20.0` | `1e4` (the march length is set by the screen) |
| `SsrParams::steps` | `64` | `0` (one step a pixel of the screen segment) |
| `raw::kApiSnapshot` | 2 | 3 |

## What was added

- `SsrParams::hiddenEntry` (default true): end the march as a miss inside a solid entered through a
  hidden face. It needs the back layer.
- `ViewGBuffer::back`: the view depth of the exit of the solid seen at each pixel.
- `post::addBackLayer()`: fills `back` with a second, peeled raster pass of the scene.
- `RasterOptions::peelAfter` and `RasterOptions::backFacing`: the depth peel and the facing record
  that pass uses.

## How to update

Code that wants the snapshot 2 behaviour sets `maxDistance = 20.0`, `steps = 64` and
`hiddenEntry = false`. Code that wants the new SSR calls `addBackLayer()` on its view G-buffer before
`ssr()`; without it, `ssr()` marches per pixel but cannot apply the hidden-entry rule.

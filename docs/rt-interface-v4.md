# Path tracer interface v4: announcement and design

Written 2026-10-31. This note is the announcement that `raw/renderer/rt_pathtrace.hpp` requires
before an interface change merges. Its readers are the two consumers of the tracer's buffers: the
hardware session that trains learned ray reconstruction, and the shader lab that stylises from them.

## Why the version moves

Three defects in the tracer that every plain (v2) scene and the GPU tracer run on.

| Check | Interface v3 | Interface v4 |
|---|---|---|
| e1: the lobe alone, 192 white cases | 184 fail. Dielectric at specular 1, roughness 0.05, mu 0.05 reflects 1.780. Metal at roughness 1 keeps 0.307 | All pass: worst deviation 2.8e-3 at 2^20 samples, 6.8e-4 at 2^23 |
| e2: white plane at the defaults, mean | Default dielectric 1.0225. Specular 1, roughness 0.1: 1.1392. Metal, roughness 1: 0.4584 | 0.9996, 0.9997, 0.9994 |
| e3: closed box at the defaults, share of the answer | 0.863 at albedo 0.8, 0.612 at 0.9 | 0.997 at 0.8, 0.999 at 0.9 |
| Rays through shared edges and vertices of closed meshes that hit nothing | 1589 of 19824 | 0 of 19824 |

`evidence/rt-v4-energy.json` holds the runs. `tests/test_rt_pt_energy.cpp` and
`tests/test_primitives.cpp` hold the checks, and `RtPtParity` runs e2 and e3 on the GPU.

## The lobe

`rt_pt_math.hpp`'s `evalBsdf` and `pt.wgsl`'s `eval_bsdf` now match `raw/renderer/pbr.hpp`'s energy
model without its extensions: GGX with height-correlated Smith and Schlick Fresnel, Kulla-Conty
multiple scattering, and a Lambert base weighted by (1 - E_s(mu_o)) (1 - E_s(mu_i)) / (1 - E_s_avg).
The inputs are the split albedos A and B of the white lobe, in a table of 64 columns of sqrt(mu) by
128 rows of roughness (`rt_pt_energy.cpp`). The table is built once a process, in float64, and held
as float32. The GPU tracer reads the same floats from the end of its material buffer, so both sides
evaluate one function. Sampling and the pdf are unchanged. A material with no specular lobe
(metallic 0, specular 0) stays pure Lambert.

`PtControls::legacyLobeV2` restores v3's lobe. It exists as the false control of e1 and e2.

## The bounce limit: raised, not removed

`PathTraceDesc::maxBounces` moves from 8 to 64, and the `colonnade` command follows. Russian roulette
from bounce 3 ends most paths long before that, so the limit adds little cost on ordinary scenes.
Measured on an RTX 4090 with D3D12, `retro_room` at 1920 x 1080 x 64 spp took 1225 ms at v3 and
1638 ms at v4 (lobe, triangle test and limit together, one run each). `raw_hall` at 3840 x 2160 x
16 spp took 4921 ms and 5089 ms, with no device removal.

I kept a hard limit because it bounds the shader's loop and because `rt-pathtrace --bounces` already
caps at 64. It leaves a known bias in a closed scene: a share a^64 of the energy at albedo a. This
table is derived from the geometric series. Albedos 0.8 and 0.9 were also measured (e3).

| Albedo | Energy lost at 64 bounces |
|---|---|
| 0.8 | 6e-7 |
| 0.9 | 1.2e-3 |
| 0.95 | 3.8% |
| 0.98 | 27% |
| 0.99 | 53% |

Paths in open scenes escape, so those scenes lose less. A closed interior of near-white surfaces can
still read dark. Removing the limit past a minimum depth would fix that, and it needs its own test of
the GPU loop and the watchdog.

Two commands keep 8 on purpose. `pt-error` and `material-gallery --pt` default to it because
`evidence/m3-verified-scenes-bounds.json` and `evidence/m3-lighting-v2-bounds.json` state their bounds
at 8 bounces. Those references therefore still carry the cut.

## The triangle test

`raw::intersectTri` decides the hit by the signs of the sheared edge functions of Woop, Benthin and
Wald, "Watertight Ray/Triangle Intersection", JCGT 2(1), 2013. Then t, u and v come from
Moeller-Trumbore's operations, because the sheared t carries more rounding: with the paper's t, GPU
and CPU disagreed past the 1e-5 relative bound of B2 on 3121 rays. A ray the old test accepted keeps
its t, u and v to the bit. The sheared values are used only where the old determinant is under 1e-7,
which are rays the old test lost. u and v are clamped to the triangle for a hit the old test refused.
The shaders mirror all of this.

Limits:

- The paper's float64 recomputation of an edge function of exactly 0 is left out, so CPU and GPU run
  the same test. A zero counts as inside on both neighbours, and the closest-hit order breaks the tie.
- The leak test runs on the CPU. A shader compiler that contracts `a * b - c * d` to a fused operation
  would break the exact negation between neighbours, and no GPU leak test exists yet.

## What moved

The compatibility promise of `docs/rt-interface-v3.md` ends here.

- **Every plain scene with a specular lobe, on CPU and GPU:** radiance changes by design. The pins in
  `tests/test_rt_pt_v2_hashes.cpp` were re-recorded for GCC and MSVC. The furnace scene there moved
  too. Its material has specular 0, so the lobe is unchanged, which leaves the triangle test as the
  cause (inferred; the flipped rays were not traced).
- **`evidence/identity-golden.json`, case `wide-512` only:** `frame.ppm`, `frame_hdr.pfm`,
  `certificate.json` and `receipt.json`. The rasteriser's frame has no path traced lobe, so this comes
  from the triangle test: 1 pixel of 262144 moves by one 8-bit level. Which ray flipped was not traced.
  The other files of the matrix, the glTF goldens and the lighting model 1 goldens are unchanged.
- **`tests/test_colonnade.cpp`:** unchanged. It uses the full v3 material model.

## Consequence for ray reconstruction: reference frames are biased

**Every `rr` reference and training frame rendered before v4 is biased.** `rr-render` uses this lobe
and the default bounce limit, so those frames hold the energy errors above: bright rims at grazing
angles on dielectrics, dark rough metals, and interreflection missing past 8 bounces. A model trained
on them learned those errors as its target, and its measured error against those references cannot
show them. `rr_manifest.py` records the interface version in each manifest's `renderer` string, which
identifies the affected frames. No frame was re-rendered and no model retrained in this change.

## Cost

CPU, `retro_room` at 640 x 360 x 16 spp, 8 bounces on both: 534 ms at v3 and 764 ms at v4 (one run
each), from the sheared test on every candidate triangle and the table lookups. An 8 x 8 render at
1 spp takes 147 ms against 74 ms, which puts the table's build near 70 ms a process on 24 threads.

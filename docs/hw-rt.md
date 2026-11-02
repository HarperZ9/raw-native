# Hardware ray tracing and spectral caustics

raw-native traces rays on the GPU's ray-tracing hardware through DXR inline ray queries (D3D12,
tier 1.1). Two things run on it today:

- **The path tracer** (`hw-pt`): the compute path tracer's own passes with one function swapped, so
  any caller can move from our compute BVH to the hardware without other change.
- **Spectral caustics through measured glass** (`caustic`, effect C1): white light split by a prism of
  SCHOTT N-SF11 and focused by an N-BK7 lens, one wavelength a path, from the maker's dispersion data.

Both are held to their CPU references by a tolerance contract that planted bugs must fail.

```sh
cmake -S . -B build -DRAW_NATIVE_GPU_D3D12=ON && cmake --build build --config Release
build/Release/raw_native_cli hw-pt render --scene raw_hall --width 3840 --height 2160 --spp 64 --out hall
build/Release/raw_native_cli hw-pt contract --out hw-pt.json
build/Release/raw_native_cli caustic render --width 3840 --height 2160 --spp 4096 --photons 4096 --exposure 40 --out prism
build/Release/raw_native_cli caustic contract --out caustic.json
build/Release/raw_native_cli caustic render --cpu --width 320 --height 180 --out prism-cpu   # the CPU reference
```

## Results on the RTX 4090

| What | Number | Evidence |
|---|---|---|
| Path tracer, hardware against compute BVH, 3840 x 2160, 64 spp, wall time | raw_hall 2.0 s against 15.4 s (7.6x); retro_room 1.75 against 6.0 s (3.4x); iso_street 1.47 against 2.12 s (1.4x); one run each | `evidence/hw-h2-pt-timing-rtx4090.json` |
| Path tracer contract (H2.0) | every bounded check met; all three planted bugs fail it; an instance moved by 0.1% of the scene diagonal already fails it | `evidence/hw-h2-pt-runs.json` |
| Caustic frame, 3840 x 2160, 1 camera sample and 1 photon a pixel | 2.33 ms GPU (p5 2.31, p95 2.36); 1.30 J a frame, 1.13 J above idle | `evidence/hw-c1-caustic-timing-rtx4090.json`, `-energy-` |
| 4K still, 4096 samples and 4096 photons a pixel | 9.4 s GPU, 12.0 s wall | `evidence/hw-c1-caustic-runs.json` |
| Caustic contract (C1) | K1 (hardware against CPU) and the three controls met; **K2 (the two estimators against each other) fails** its per-pixel bound while the image sums agree within 0.04% | `evidence/hw-c1-caustic-runs.json` |

Verified: the numbers above were read from the evidence files of the runs on 2026-11-02, the 4K
still was opened and inspected at full size and in 1:1 crops, and ctest passed 107 of 110 with the
three failures already recorded before this change. Not verified: WARP, other GPUs, and the cause of
K2's failure.

## The library API

`raw/rhi/hw_rt.hpp` takes plain data, so a tool with only triangles and rays can use it (a lens
bench, a lidar or depth-camera simulation, a CAD viewer):

```cpp
#include "raw/rhi/hw_rt.hpp"
std::string err;
auto rc = raw::rhi::hw::RayCaster::create(device, triangles, /*allowUpdate=*/true, err);
std::vector<raw::rhi::hw::HitOut> hits;
rc->closest(rays, hits, err);     // RayIn {origin, tmin, direction, tmax}; HitOut {t, u, v, tri}
rc->refit(movedTriangles, err);   // same triangle count: bottom level updated, top level rebuilt
```

- `RayCaster` keeps the bottom- and top-level structures across calls, refits when vertices move and
  reports GPU times of the last build or refit and the last trace.
- `pathTracePasses` and `spectralPasses` are the two tracers' passes over a hardware structure;
  `raw/renderer/rt_pt_hw.hpp` and `raw/renderer/rt_caustic.hpp` wrap them for raw-native's scenes.
- Without DXR tier 1.1, on the null and WebGPU backends, or with `RAW_NATIVE_HW_DISABLE=ray_query`,
  every entry point returns `ran = false` or an error with the reason. `hw-pt render` then takes the
  compute tracer and records why; `caustic` exits 1 and `--cpu` renders on the CPU.

Hits follow H1.1's contract (`evidence/hw-h1-1-bounds.json`): t within 1e-5 t + 2^-20 L of the CPU's,
barycentrics within 1e-4 + 2^-18 L / h, and triangle disagreements only at edges, ties, grazing angles
or the ray's ends.

## How the hardware path tracer stays the same tracer

`scripts/hw_pt_hlsl.py` generates `src/rhi/d3d12/hw_shaders/hw_pt.hlsl` from the compute tracer's
`pt_trace.hlsl` and replaces exactly one function, `trace`, with an inline ray query
(`RAY_FLAG_FORCE_OPAQUE`, and `ACCEPT_FIRST_HIT_AND_END_SEARCH` for shadow rays, TMin 1e-4). Every
other line, including the random numbers, stays byte for byte, and CI fails if the two drift
(`hw_pt_hlsl.py --check`). With the same seed, hardware and compute give bit-identical radiance on 6
to 90% of pixels depending on the scene, and a mean relative difference of 1e-5 to 3e-3.

## The equivalence contract

Hardware traversal is not bit-identical to ours: the driver's triangle test, barycentrics and order
among equal distances differ, so a few paths branch differently. Equivalence is therefore stated on
error statistics of two Monte Carlo estimates of the same image at matched sample counts.

- **Per pixel**, z = (a - b) / sqrt(var_a + var_b), with variances from each tracer's own output (path
  tracer) or from 16 independent batches a side (caustics, per channel X, Y, Z).
- **Bound**: mean |z| at most 0.9 (0.80 is expected for normal noise), at most 0.2% (path tracer) or
  0.5% (caustics) of values past 4, the image sums within 3 standard errors, nothing non-finite.
- **Caustic region** (added after run 1, see below): the same bound over the pixels where light paths
  carry at least 5% of the luminance.
- **Planted bugs that must fail**: next-event weight forced to 1, the Lambert cosine dropped, the
  instance moved by 1% of the scene diagonal (path tracer); no dispersion, the splat cosine dropped,
  the 1% offset (caustics). A sweep of smaller offsets and index errors measures what the contract can
  and cannot see.

What it cannot see, measured: on the caustic scene at 192 x 108 and 64 samples, a constant error in
every glass index up to 3e-3 (about the gap between neighbouring catalogue glasses) passes. Index
errors of that size need a dedicated measurement, such as the lens bench of EN1 in the roadmap.

## Effect C1: why spectral caustics

Three candidates were weighed against the prior art (`project-docs/PRIOR-ART.md`):

- **Spectral caustics through measured glass** had every input in hand (catalogue Sellmeier rows,
  CIE colour matching, the Lens Room as the visual target), no 2024 to 2026 paper doing it in real
  time from catalogue data was found, and the same tracer serves lens design. It needed no
  ray-tracing pipeline and touched no existing interface.
- **GI from hardware rays with a surfel or probe cache** has a work-in-progress branch for split
  radiance cascades and needs path tracer interface v3 on the GPU first. Higher value later, higher
  risk now.
- **Thin-film interference** already exists as a material term (`pbr_iridescence.cpp`, after Belcour
  and Barla), so it would add least.

### The light transport

One wavelength a path, uniform over 380 to 780 nm, weighted by the CIE 1931 2 degree colour matching
functions. Glass refracts and reflects by the exact unpolarised Fresnel equations at that wavelength's
Sellmeier index. Two estimators add up to the image:

- **Camera paths** estimate direct light at diffuse surfaces toward the lamps and count a lamp seen
  with no diffuse surface before it (directly or through glass).
- **Light paths** leave a lamp, pass through glass at least once, and join every diffuse surface they
  reach to the pinhole camera. These are the caustics, sharp at low cost.

One class of light is left out on both CPU and GPU: light that leaves a lamp through glass, scatters
diffusely and reaches the camera through glass again (a caustic seen through the prism). The
BSDF-only estimator (camera paths that count every lamp hit) is complete and serves as the check.

| Input | Value | Status and source |
|---|---|---|
| Prism glass | SCHOTT N-SF11, nd 1.78472, vd 25.68, Sellmeier formula 2 | measured (maker's catalogue fit), refractiveindex.info release 2026-05-24, CC0 |
| Lens glass | SCHOTT N-BK7, nd 1.5168, vd 64.17 | same |
| Observer | CIE 1931 2 degree, 5 nm | standard table, `src/renderer/data/cie1931.hpp` |
| Lamps | CIE illuminant A (Planck, 2848 K, c2 = 1.435e-2 m K) | defined by the CIE equation, not measured |
| Diffuse albedos | 0.8 floor, 0.6 walls, 0.05 stands, flat | chosen |

`tests/test_caustic.cpp` checks on every build that both glasses reproduce their catalogue nd within
5e-5 and vd within 0.05, that illuminant A matches the CIE table, and that every glass triangle faces
outward.

### What failed, and what changed

- **Run 1.** The no-dispersion control passed the whole-image test: the spectrum covers under 1% of
  the pixels. Addendum 1 added the caustic-region statistic, which catches it.
- **Run 1 and the diagnosis after it.** The two estimators disagreed by 1.5 to 3% even with the glass
  removed, and the gap moved when only the random dimension of the wavelength moved. The random
  numbers (the path tracer's PCG2D over pixel and sample * 64 + dimension) were not independent enough
  for this tracer; a nested lowbias32 hash brought the gap inside the seed-to-seed spread. **The path
  tracer uses the same construction and was not retested here.**
- **Runs 2 and 3.** K2 still fails per pixel (1.6% of values past 4) while the image sums agree
  within 0.04%. A calibration row (BSDF-only against itself) passes, so the reference side is not the
  cause. The cause is open; `evidence/hw-c1-caustic-runs.json` lists the next measurements.

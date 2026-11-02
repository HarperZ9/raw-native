# Prior art: hardware ray tracing and new effects (2026-11-02)

Written before choosing the work in `docs/hw-rt.md`, so nothing that existed was rebuilt. Sources:
this repository at origin/main d317bc7 and its branches, superframe at origin/main c031d0e, the
author's study folders and saves map, the session recall index, and the 2026-11-02 rendering survey.

## In this repository

| Found | Where | What it gave this work |
|---|---|---|
| HW H1.0: probe of DXR tiers, mesh shaders, wave ops and more, with forced-off fallbacks | `src/rhi/d3d12/hw_probe.cpp`, `raw/rhi/hw.hpp`, `evidence/hw-h1-0-*` | the feature switch `RAW_NATIVE_HW_DISABLE=ray_query` and the fallback rule |
| HW H1.1: closest hits by DXR 1.1 inline ray query, one BLAS and one TLAS, against the CPU BVH, with offset and rotation controls | `src/rhi/d3d12/hw_rayquery.cpp`, `evidence/hw-h1-1-*` | the build code (generalised into `hw_accel.cpp`), the hit tolerances, and the planted-offset idea. Its open results: coplanar ties and two grazing hits miss the bound on the RTX 4090 |
| The compute path tracer, interface v4, with CPU reference and P1 to P4 parity, including P2's z test | `src/renderer/gpu/rt_pt_gpu.cpp`, `pt.wgsl`, `evidence/rt-r2-bounds.json`, `docs/rt-interface-v4.md` (merged as #228 and #229) | the tracer the hardware path reuses, and the statistic of the contract |
| GPU PLOC BVH build and stack traversal | `rt_bounds_*`, `rt_ploc_*`, `rt_trace.hlsl` | the compute baseline the hardware is timed against |
| Thin-film iridescence (Belcour and Barla, KHR_materials_iridescence), with a spectral reference | `src/renderer/pbr_iridescence.cpp` | ruled thin film out as the new effect |
| Sellmeier glasses N-BK7 and N-SF2 in the lens style pass; 1609 catalogue glasses with provenance | `src/renderer/style_lens_plan.cpp`, `web/shaders/lens/glass-data.mjs`, `docs/shaders/CONSTANTS.md` | the measured glass rows C1 uses |
| CIE 1931 colour matching functions | `src/renderer/data/cie1931.hpp` | C1's observer |
| Measured film-stock library (#230) | `src/renderer/style_film_stocks.cpp` | a later camera stage for C1's spectral output |
| Branches: rt-open-failures (R1 to R3 failures narrowed on the RTX 4090), p6-radiance-cascades (work in progress, CPU reference and passes, unpushed) | local worktrees | none of the 11 unmerged remote branches touches ray tracing, GI, spectral rendering or lenses; cascades are the GI candidate's prior work |
| No caustics anywhere in `src` or `raw`; the path tracer has no transmission and a single index | search of the tree | C1 had to bring its own dielectric |

## In superframe (origin/main)

- `packages/materials`: 111 records of spectral n and k at 380 to 780 nm, 15 dielectrics with
  tabulated dispersion (bk7, fused silica, sapphire and others), provenance per record.
- `packages/measured`: provenance-stamped tables; the SCHOTT catalogue parse (Sellmeier and
  stress-optical constants) is kept private by licence and listed only by hash.
- `packages/joules`: NVML total-energy counter through ctypes (`Window`), used here for energy per
  frame.
- `packages/rooms`: the lens, sound, PCB, fluid and map rooms; the lens room's core has a thin-film
  transfer matrix and Jones matrices.
- No lens-design package on main: the optical tracer, glass catalogue and tolerance tools sit in a
  study folder, not yet a package.

## The author's studies

- **The Lens Room** (2026-10-31): a browser ray marcher through lenses, trains of up to 36 surfaces
  and graded-index worlds, with real caustics and a caustic metric. Glass by nd and Abbe number with
  Cauchy dispersion and three wavelengths; no Sellmeier. It is the visual target C1 answers with
  catalogue dispersion and a full spectrum.
- **SDF, spherical harmonics and modal bases** (2026-10-31): one Laplacian eigenbasis served heat,
  room sound and fog light; nine SH coefficients hold irradiance to 1% under overcast sky and miss by
  19 to 27% with a sun. Relevant to the GI candidate's cache.
- **Lens geometry, lens pipeline, reshaping lenses, lens spectrum, lens metamers** (2026-10-29 to
  11-01): shape kernels, an optical design pipeline with blind rules near 31% false accept, and
  preregistered wave studies. They make the lens bench (roadmap EN1) the natural next consumer.
- **Saves leads**: precomputed radiance transport after Ng 2003 for the Lens Room (queue H79, map
  row 548); WebGPU surfel cascades with no published numbers (row 549); a small network replacing
  in-scattering in a cloud march (H78, row 501); single-pass compute SMAA, still open (row 547).

## The 2026-11-02 rendering survey

- C3 and gap G1: no 2024 to 2026 paper on real-time dispersion from catalogue glass was found.
  C1 starts on that gap; HR1 in the roadmap states the real-time target.
- R1 (Galgoczi 2026, arXiv 2608.21396): shader execution reordering cut kernel cycles 16.55x in
  GPU photon Monte Carlo on an RTX 4090. C1's light pass is the same shape (roadmap HR6).
- L1 (Chen et al. 2026, arXiv 2605.04017): precomputed lens transport maps, 12 to 15x faster than
  tracing, for fixed symmetric lenses (roadmap EN1).
- S1 (Moon et al., SIGGRAPH Asia 2025): hyperspectral polarimetric BRDFs at 68 bands (roadmap HR4).
- E1.1, E2.1, E4.1, E5: stray light at Rubin, automotive lidar simulation, trimmed-surface
  containment, ray-tracing hardware for collision and Monte Carlo (the roadmap's engineering track).
- G6 and G10: energy per converged frame, and per-pixel error bars tested for coverage (HR7, HR8).

## Recall index

Sessions of 2026-10-11 restarted the ray-tracing and hardware work after a usage stop; a 2026-10-23
spectral path tracer on SDFs and a 2026-10-30 site hero with BK7 dispersion and water caustics exist
outside this repository; a 2026-10-31 note records that the path tracer had no dispersion. No surfel
GI work was found.

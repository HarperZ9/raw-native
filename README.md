# raw-native

raw-native renders a 3D scene on the CPU and tells you whether its fast lighting
shortcut can be trusted. It computes ambient occlusion twice: once with a cheap
screen-space approximation, the kind real-time engines use, and once with a
ray-traced reference. It then measures how far the shortcut drifts from the
reference and writes the answer into a small JSON certificate. The verdict is
`verified` when the drift stays inside the tolerance and `refuted` when it does
not. You get the picture and the evidence for it in the same run.

It is written in C++23 with no third-party dependencies, no GPU and no graphics
API. Every pixel comes from the standard library and the engine's own code, so
it builds the same way on any C++23 toolchain.

| Shaded frame | Ray-traced AO (reference) | Screen-space AO (shortcut) | Error map |
|---|---|---|---|
| ![frame](docs/images/default-frame.png) | ![ray-traced AO](docs/images/default-ao_rt.png) | ![screen-space AO](docs/images/default-ao_ss.png) | ![error](docs/images/default-ao_error.png) |

The default view above, rendered at 512 x 512, comes back `refuted`: the
shortcut's error is 0.135 RMSE against a tolerance of 0.12. Its certificate:

```json
{"claim":"screen-space AO matches ray-traced ground truth within tolerance",
 "verdict":"refuted","oracle":"raw-rt-ao-v1",
 "evidence":[["pixels","151984"],["rmse","0.1349"],["maxError","0.6406"],["tolerance","0.1200"]],
 "channels":{"ao_fidelity":0.881125,"motion_coherence":1,"hdr_headroom":0.875542}}
```

## Run it now

Download a prebuilt binary from the
[latest release](https://github.com/HarperZ9/raw-native/releases/latest)
(Windows x64 or Linux x64), check it against `SHA256SUMS`, and run:

```sh
raw_native_cli --out ./out
```

Or build from source. You need CMake 3.24 or newer and a C++23 compiler
(tested with MSVC 19.50 and GCC 13.3).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/raw_native_cli --out ./out        # on Windows: build\Release\raw_native_cli.exe
```

## What one run writes

| File | Contents |
|---|---|
| `frame.ppm` | The shaded frame, 8-bit, for people to look at |
| `frame_hdr.pfm` | The same frame as unclamped linear radiance, for programs to read |
| `ao_rt.pgm` | Ray-traced ambient occlusion, the reference |
| `ao_ss.pgm` | Screen-space ambient occlusion, the shortcut |
| `ao_error.pgm` | Per-pixel absolute difference between the two |
| `certificate.json` | Claim, verdict, oracle and evidence for the AO comparison |
| `arena_certificate.json` | Proof the render stayed inside its memory budget |
| `channels.json` | The certificate plus camera, coverage, depth, normal, motion, HDR and an 8x8 luminance readout |

Exit code 0 means the frame rendered. Exit code 1 means the render tried to use
more memory than its budget and stopped; it still writes a memory certificate,
with the verdict `refuted`. Exit code 2 means bad input.

## Point the camera anywhere

```sh
raw_native_cli --out ./out --width 512 --height 512 --eye 0,9,3 --target 0,0.5,0
raw_native_cli --out ./out --params view.json          # flags given after it override the file
raw_native_cli --out ./out --prev-eye 4.3,4,5.7        # a previous camera produces motion vectors
raw_native_cli --help
```

A params file is a flat JSON object with any of `out`, `width`, `height`, `eye`,
`target`, `up`, `fovy`, `prev_eye`, `prev_target` and `prev_up`.

## Measured results

All numbers come from the v0.2.0 CLI on one machine (Intel Core i7-13700KF,
Windows 11) unless noted.

- **Tests:** 29 of 29 CTest targets pass in Release and Debug with MSVC 19.50,
  and 29 of 29 in Release with GCC 13.3 on Ubuntu 24.04.
- **Reproducible output:** the default render's images, `certificate.json` and
  `channels.json` are byte-identical across MSVC Release, MSVC Debug and GCC 13.3
  on Linux (SHA-256 compared). The memory certificate differs between Debug and
  Release because Debug builds allocate more; that is expected.
- **The verdict depends on the view.** Same scene, 512 x 512, tolerance 0.12:

| View | Flags | Covered pixels | RMSE | Verdict |
|---|---|---|---|---|
| Default | none | 151,984 | 0.1349 | refuted |
| High | `--eye 0,9,3 --target 0,0.5,0` | 239,854 | 0.0827 | verified |
| Low | `--eye 6,1.5,2 --target 0,1,0` | 140,838 | 0.1659 | refuted |
| Close | `--eye 2,2.5,3 --target 0,0.8,0 --fovy 0.7` | 214,043 | 0.2167 | refuted |
| Wide | `--eye 5,5,8 --target 0,0.5,0 --fovy 1.2` | 88,869 | 0.0882 | verified |

- **Speed:** about 1.2 s per run at 256 x 256 and 4.6 s at 512 x 512, single
  threaded, measured wall time including the memory-measuring pass described
  below. The machine was shared with other work while timing.

Limits, stated plainly:

- The scene is one built-in test scene: a box on a ground plane under one
  directional light. There is no model loader yet.
- The ray-traced reference uses 64 hemisphere samples per pixel drawn from a
  deterministic per-pixel hash. It is a repeatable estimate of the integral and
  shows visible grain.
- The screen-space method is a simple one with 24 samples per pixel. It misses
  most of the occlusion the reference finds, so most views come back `refuted`.
  The check is reporting a weak shortcut there.
- The tolerance (0.12 RMSE) is fixed in the CLI.
- Byte-identical output was checked on x86-64 with two compilers. Other CPU
  architectures and compilers are untested.

## How the memory budget works

The CLI renders twice. The first pass runs inside a generous slab and records
the exact number of bytes the frame needs. The second pass renders again inside
a budget of exactly that many bytes, using a bump allocator that refuses any
request past the budget and never grows. If anything allocates more the
second time, the render stops and the memory certificate says `refuted`.

## Use it as a library

`cmake --install build --prefix <dir>` installs the CLI, the static library, the
headers and a CMake package. Then, in your project:

```cmake
find_package(raw_native 0.2 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE raw_native::raw_native)
```

## Layout

```
raw/     headers: vectors, matrices, images, scene, G-buffer, rasterizer,
         ray-traced AO, SSAO, reconcile, certificate, composite, arena, motion
src/     implementation
app/     command-line driver
tests/   one test executable per test_*.cpp
docs/    example images and certificates
```

## License

MIT. See [LICENSE](LICENSE). Copyright (c) 2026 Zain Dana Harper.

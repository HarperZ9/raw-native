# raw-native

raw-native renders a 3D scene on the CPU and tells you whether its fast lighting
shortcut can be trusted. It computes ambient occlusion twice: once with a cheap
screen-space approximation, the kind real-time engines use, and once with a
ray-traced reference. It then measures how far the shortcut drifts from the
reference and writes the answer into a small JSON certificate. The verdict is
`verified` when the drift stays inside the tolerance and `refuted` when it does
not. You get the picture and the evidence for it in the same run.

It is written in C++23 with no external dependencies, no GPU and no graphics
API. The one file it did not write itself is `superstack.hpp`, the FSL-1.1-MIT header of
the shared receipt contract, vendored and pinned by hash. Every pixel comes from the standard library and the engine's own code, so
it builds the same way on any C++23 toolchain. The same code also runs in a web
browser as WebAssembly and writes byte-identical files. Two optional builds
render the frame on the GPU, natively through D3D12 on Windows or through
WebGPU in a browser, and check every GPU frame against this CPU path. The
default build contains no GPU code.

| Shaded frame | Ray-traced AO (reference) | Screen-space AO (shortcut) | Error map |
|---|---|---|---|
| ![frame](docs/images/default-frame.png) | ![ray-traced AO](docs/images/default-ao_rt.png) | ![screen-space AO](docs/images/default-ao_ss.png) | ![error](docs/images/default-ao_error.png) |

The default view above, rendered at 512 x 512, comes back `refuted`: the
shortcut's error is 0.135 RMSE against a tolerance of 0.12. Its certificate,
with the output digests shortened here:

```json
{"claim":"screen-space AO matches ray-traced ground truth within tolerance",
 "verdict":"refuted","oracle":"raw-rt-ao-v1",
 "evidence":[["pixels","151984"],["rmse","0.1349"],["maxError","0.6406"],["tolerance","0.1200"]],
 "channels":{"ao_fidelity":0.881125,"motion_coherence":1,"hdr_headroom":0.875542},
 "schema":"raw-cert/2","renderer":"raw-native 0.5.1",
 "params":{"eye":[4,4,6],"fovy":0.899999976,"height":512,"prev_eye":null,"prev_target":null,
           "prev_up":null,"rt":true,"target":[0,1,0],"tolerance":0.119999997,"up":[0,1,0],"width":512},
 "samples":{"rt":64,"ss":24},
 "exact":{"pixels":151984,"rmse":0.134912357,"maxError":0.640625,"tolerance":0.119999997},
 "outputs":{"ao_rt.pfm":"eb60b22d4151...","ao_ss.pfm":"fddc169c7fca...","mask.pgm":"892aacd21cae...","...":"..."}}
```

The first fields are the 0.2.0 certificate, unchanged. Everything from `schema`
on is new in 0.3.0: the renderer version, every parameter that changes the
pixels, the sample counts, the reconcile values at full float precision and the
SHA-256 of every file the verdict was judged from.

## Run it now

Download a prebuilt binary from the
[latest release](https://github.com/HarperZ9/raw-native/releases/latest)
(Windows x64, Linux x64 or WebAssembly), check it against `SHA256SUMS`, and run:

```sh
raw_native_cli --out ./out
raw_native_cli verify ./out
```

Or build from source. You need CMake 3.24 or newer and a C++23 compiler
(tested with MSVC 19.50 and GCC 13.3). The WebAssembly build is pinned to
CMake 4.2.0 and emsdk 6.0.11, so its bytes match the release; see
[Run it in a browser](#run-it-in-a-browser).

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
| `ao_rt.pfm`, `ao_rt.pgm` | Ray-traced ambient occlusion, the reference, as 32-bit float and as an 8-bit preview |
| `ao_ss.pfm`, `ao_ss.pgm` | Screen-space ambient occlusion, the shortcut, as 32-bit float and as an 8-bit preview |
| `mask.pgm` | Coverage mask: 255 where a surface covers the pixel. The reconcile counts only these pixels |
| `ao_error.pgm` | Per-pixel absolute difference between the two |
| `certificate.json` | Claim, verdict, oracle, evidence and the `raw-cert/2` provenance for the AO comparison |
| `arena_certificate.json` | Proof the render stayed inside its memory budget |
| `channels.json` | The certificate plus camera, coverage, depth, normal, motion, HDR and an 8x8 luminance readout |
| `receipt.json` | The same AO check as a `superstack.receipt/1` receipt (from 0.5.0), described below |

Exit code 0 means the frame rendered. Exit code 1 means the render tried to use
more memory than its budget and stopped; it still writes a memory certificate,
with the verdict `refuted`. Exit code 2 means bad input.

## The superstack receipt

From 0.5.0 every render also writes `receipt.json`, a `superstack.receipt/1`
receipt from the [superstack](https://github.com/HarperZ9/superstack) contract,
which several of the author's renderers and sound engines share. `certificate.json`
is unchanged, byte for byte, so existing readers keep working.

The receipt restates the AO check in the contract's form. It is canonical JSON
with a SHA-256 seal over every field, and it reports two verdicts separately:

- **identity**, `MATCH` when the subject's bytes equal the reference's and
  `DRIFT` otherwise. The subject is the screen-space AO as float32 and the
  reference is the ray-traced AO, two different estimators, so this reads
  `DRIFT` by design.
- **tolerance**, `verified`, `refuted` or `unverifiable` with a reason: the
  certificate's verdict, with the same RMSE, maximum error and bound.

It also records the scene in the contract's scene format, the hash of the
frame's raw RGB8 bytes (`frame.rgb8`), the digest of every file written and
four `does_not_prove` lines. For the default view, the scene hash and the
frame hash equal the contract's published reference scene and pixel reference,
and the ray-traced AO bytes equal the hash superstack's separate Python
renderer produced.

A `--gpu` run adds `gpu_receipt.json`: the GPU frame's RGB8 bytes against the
CPU frame's, with identity (`MATCH` when the GPU drew the same 8-bit frame) and
the tolerance verdict of `gpu_certificate.json`, which now reports the same
`identity` beside its `verdict`.

`raw_native_cli verify` checks the receipt too: the seal, every digest, the
subject and reference hashes recomputed from the float files, and the
tolerance verdict against the certificate's.

The WebAssembly build writes the same receipt. From 0.5.1, raw-native vendors
superstack 0.2.0, whose header compiles with libc++, the standard library
Emscripten uses. The wasm build's `receipt.json` is byte-identical to the
receipts from MSVC on Windows and GCC on Linux, and `raw_native_cli verify`
passes on it. In 0.5.0 the wasm build wrote no receipt, because superstack
0.1.0's header did not compile with libc++.

## Check a certificate without trusting the renderer

`raw_native_cli verify <dir>` renders nothing. It re-hashes every file the
certificate lists, recomputes the reconcile from `ao_rt.pfm`, `ao_ss.pfm` and
`mask.pgm`, and compares pixel count, RMSE, maximum error and verdict with the
recorded values. Exit code 0 means every check matches, 3 means a mismatch and
2 means a file is missing.

`scripts/recheck.py <dir>` does the same in Python with the standard library
only. It shares no code with the C++ verifier, so a bug in one shows up as a
disagreement with the other. Both reproduce the recorded RMSE bit for bit,
because the float buffers on disk are the ones the renderer measured.

To replay a render, pass the certificate's `params` object back as a params
file. The same build on the same platform gives the same output digests.

## Run it in a browser

The release includes a WebAssembly build: `raw-native.wasm`, its Emscripten
module `raw-native.mjs` and a small loader, `raw-loader.mjs`. The loader fetches
both files, checks each against the SHA-256 you pass in, and only then runs
them. It works on the page or in a Worker.

```js
import { loadRawNative } from "./raw-loader.mjs";
const raw = await loadRawNative({
  moduleUrl: "raw-native.mjs", moduleSha256: "<from SHA256SUMS>",
  wasmUrl: "raw-native.wasm",  wasmSha256: "<from SHA256SUMS>",
});
const run = raw.render({ width: 256, height: 256, eye: [0, 9, 3], target: [0, 0.5, 0] });
run.certificate;  // the same raw-cert/2 certificate the native CLI writes
run.frame;        // { width, height, rgba }
```

To build it yourself, install [emsdk](https://github.com/emscripten-core/emsdk)
6.0.11 and CMake 4.2.0, activate emsdk in your shell, and run:

```sh
cmake --preset wasm
cmake --build --preset wasm      # build-wasm/raw-native.mjs and raw-native.wasm
node wasm/run-node.mjs build-wasm/raw-native.mjs ./out-wasm
python scripts/compare_renders.py ./out ./out-wasm
```

The release's wasm files are built with emsdk 6.0.11 and CMake 4.2.0. Both
versions are pinned, because both change the wasm bytes. CMake 4.2 adds
`-fPIC` to every Emscripten compile, so a build configured with CMake 3.29
differs by bytes from one configured with 4.2.0, and still writes the same
output files. With another CMake version, `cmake --preset wasm` prints a
warning that the wasm will not match the released bytes. CI downloads
CMake 4.2.0, checks its SHA-256 and checks that the wasm build was configured
with it.

With these versions on one host system the build is byte-for-byte repeatable:
two checkouts in different directories produce the same `raw-native.wasm`. A
build on a Linux host gives different wasm bytes than a build on a Windows host,
and both write the same output files; CI checks the Linux build against the
native renderer on every push. The released wasm was built on Windows. The presets
`wasm-simd` and `wasm-threads` build the two variants in the timing table
below.

## Render on the GPU, checked against the CPU

The same frame can render on three backends. Every GPU frame ships with
`gpu_certificate.json`, which compares it with a CPU render of the same camera
and says whether the two agree.

| Backend | Build | Runs on | Shaders | Checked on |
|---|---|---|---|---|
| CPU | the default build | any C++23 toolchain, and WebAssembly | none | the reference |
| D3D12 | `-DRAW_NATIVE_GPU_D3D12=ON`, Windows only | a hardware D3D12 adapter with shader model 6.0 | HLSL generated from the WGSL, compiled by DXC | RTX 4090, driver 610.88, Windows 11 |
| WebGPU | the `wasm-gpu` preset | a browser with WebGPU and JSPI | WGSL | RTX 4090, Chrome 154, Windows 11 |

All three run the same algorithms in the same operation order: triangle setup,
rasterization with depth, normal, position and motion channels, screen-space
AO, ray-traced AO and shading. A build without a GPU backend answers `--gpu`
with exit code 4 and an `unverifiable` GPU certificate that names the reason.

### Native D3D12

```sh
cmake -S . -B build-d3d12 -DRAW_NATIVE_GPU_D3D12=ON
cmake --build build-d3d12 --config Release
build-d3d12/Release/raw_native_cli.exe --gpu --out out-gpu --width 1440 --height 900
```

It needs the Windows SDK (10.0.18362 or newer) for `dxc.exe`, `d3d12.lib` and
`dxgi.lib`, and nothing else. The binary loads only the system `d3d12.dll` and
`dxgi.dll`. `--gpu` writes the GPU frame's files to `out-gpu`, the CPU
reference to `out-gpu/cpu`, and `gpu_certificate.json` beside them; both
directories pass `raw_native_cli verify`. The certificate records the adapter
name, PCI device id and driver version. The backend picks the first hardware
adapter in the high-performance order and never falls back to a software
adapter; `RAW_NATIVE_D3D12_WARP=1` selects WARP, the Windows software
rasterizer, on purpose.

There is one shader source. `scripts/wgsl_to_hlsl.py` translates the WGSL
passes in `src/gpu/` into `src/gpu/hlsl/`, and the generated files are
committed, so the build needs no Python. The translator accepts a strict WGSL
subset and stops on anything outside it. CI regenerates the HLSL on every push
and fails when it differs from the committed files, so the two shader trees
cannot drift apart without a red build. A hand-written HLSL copy with a parity
test would need a GPU to catch drift, and CI runners have none.

On WARP the D3D12 path reproduces the CPU reference bit for bit: all 16 renders
of the check matrix below give an RMSE of exactly 0 on every channel, and
all 16 frames are byte-identical to the CPU frame
(`evidence/d3d12-warp-checks.json`). WARP is software, so this is evidence that
the translation is exact. It is not GPU evidence. CI runs the same WARP check
on GitHub's Windows runner, and reports the hardware test as skipped, with the
reason, because the runner has no GPU.

### WebGPU in a browser

From 0.4.0 the release adds a WebGPU build, `raw-native-gpu.mjs` and
`raw-native-gpu.wasm`. It runs the WGSL passes in the browser, then renders the
same camera on the CPU and writes the same `gpu_certificate.json`.

```js
const raw = await loadRawNative({
  moduleUrl: "raw-native-gpu.mjs", moduleSha256: "<from SHA256SUMS>",
  wasmUrl: "raw-native-gpu.wasm",  wasmSha256: "<from SHA256SUMS>",
});
const run = await raw.renderAsync({ width: 512, height: 512, gpu: true });
run.frame;            // the GPU frame
run.gpuCertificate;   // raw-gpu-cert/1: GPU against the CPU reference
run.files["cpu/certificate.json"];   // the CPU reference's own certificate
```

It needs a browser with WebGPU and JSPI; it was tested in Chrome 154. Build it
with `cmake --preset wasm-gpu` and `cmake --build --preset wasm-gpu`.

### What the GPU certificate checks

Over the pixels both sides cover:

| Check | Bound |
|---|---|
| Coverage pixels that disagree | 0.2% of pixels either side covers |
| Depth, relative RMSE | 1e-4 |
| Position, normal (RMSE per component) | 1e-3 |
| Motion vectors (RMSE, UV units) | 1e-4 |
| Screen-space AO, ray-traced AO, frame (RMSE, 0..1) | 0.01 |
| The frame's own AO verdict | must equal the CPU's; RMSE within 0.005 |

The bounds and the reasoning behind them are in `raw/gpu_tolerance.hpp`, which
was committed before the first line of GPU code and before any GPU output was
seen. Both GPU backends are judged against the same bounds. Maximum errors are
reported and never bounded. The certificate also records the backend, the
adapter and driver, both render times and five `does_not_prove` lines.

## Point the camera anywhere

```sh
raw_native_cli --out ./out --width 512 --height 512 --eye 0,9,3 --target 0,0.5,0
raw_native_cli --out ./out --params view.json          # flags given after it override the file
raw_native_cli --out ./out --prev-eye 4.3,4,5.7        # a previous camera produces motion vectors
raw_native_cli --out ./out --tolerance 0.15            # recorded in the certificate
raw_native_cli --out ./out --threads 8                 # same output, faster
raw_native_cli --out ./out --no-rt                     # skip the reference; verdict is unverifiable
raw_native_cli --bench 5 --width 512 --height 512      # time 5 renders, print JSON
raw_native_cli --help
```

A params file is a flat JSON object with any of `out`, `width`, `height`, `eye`,
`target`, `up`, `fovy`, `tolerance`, `rt`, `prev_eye`, `prev_target` and
`prev_up`.

`--no-rt` shades the frame with the screen-space AO and skips the ray-traced
pass. With no reference to compare against, the certificate says
`unverifiable`, and the ray-traced files are not written.

## Measured results

All numbers come from one machine (Intel Core i7-13700KF, 24 threads, NVIDIA
RTX 4090, Windows 11 build 26220). The CPU and wasm numbers were measured on
v0.3.0, whose CPU render code later versions keep unchanged. The GPU numbers
were measured on 4 October 2026 with NVIDIA driver 610.88 (DXGI reports
32.0.16.1088), D3D12 and WebGPU in the same session.

- **Tests:** 34 of 34 CTest targets pass in Release with MSVC 19.50 and in CI
  on GitHub's Windows and Ubuntu runners. The D3D12 build adds a 35th, the
  hardware runtime test, which CI reports as skipped because the runner has
  no GPU.
- **The GPU frame matches the CPU reference, on both GPU backends.** 16 of 16
  D3D12 certificates and 16 of 16 WebGPU certificates say `verified`: three
  frame sizes and the four other views below, each with and without the
  ray-traced pass, plus a moving camera for motion vectors. Coverage agrees on
  every pixel in every case. The largest RMSE on any channel is 4.2e-5
  (ray-traced AO, low view) against a bound of 0.01, and the largest single
  pixel error is 1/64, one hemisphere ray. Each GPU frame's own AO verdict
  equals the CPU's. D3D12 and WebGPU report the same RMSE and maximum error on
  all 104 compared channel values; that is equal summaries, not a per-pixel
  comparison of the two GPU frames. The certificates also report byte
  identity of the 8-bit frames: the GPU frame equals the CPU frame (`MATCH`) in
  13 of 16 renders on both backends. The other 3 (`DRIFT`, still `verified`)
  are the ray-traced renders at 512 x 512, 1440 x 900 and the low view, where
  the ray-traced AO of a few pixels differs from the CPU by one ray (1/64).
  Evidence:
  `evidence/d3d12-rtx4090-checks.json` and
  `evidence/webgpu-rtx4090-chromium.json`.
- **GPU speed.** Median of 5 renders after one untimed warm-up, in
  milliseconds. "Every channel" includes reading back all nine buffers (about
  140 MB at 1440 x 900) and converting them for the certificate; "frame only"
  reads back the shaded frame. Both include creating the buffers, the upload,
  every pass and waiting for the GPU.

| Frame | Mode | D3D12, every channel | D3D12, frame only | WebGPU, every channel | WebGPU, frame only | native, one thread |
|---|---|---|---|---|---|---|
| 256 x 256 | Full | 5.6 | 3.9 | 11.7 | 4.9 | 258 |
| 512 x 512 | Full | 12.2 | 5.0 | 34.5 | 8.9 | 1,028 |
| 1440 x 900 | Full | 48.8 | 16.0 | 94.9 | 24.3 | 4,527 |
| 256 x 256 | No RT | 5.0 | 3.1 | 9.0 | 3.0 | 29 |
| 512 x 512 | No RT | 11.5 | 5.3 | 26.7 | 4.9 | 118 |
| 1440 x 900 | No RT | 45.7 | 13.2 | 86.3 | 15.3 | 527 |

  The WebGPU times are measured inside the wasm module in headed Chrome 154,
  so they include JavaScript promise turns and browser scheduling. They also
  move between sessions: the v0.4.0 run on the same machine measured 6.8, 14.3
  and 72.7 ms for the full frame with every channel, against 11.7, 34.5 and
  94.9 here. Read the WebGPU columns as a range, not a point. The D3D12 times
  are one run of 5 in one session, on one GPU, one driver and one OS build,
  and another adapter or driver may differ in either direction. Small gaps
  between neighbouring cells, such as 512 x 512 frame only with and without
  RT, are within run-to-run noise. The CPU column
  repeats the 0.3.0 table below. Evidence: `evidence/bench-d3d12-rtx4090.json`.
- **Reproducible output:** the default render's `frame.ppm`,
  `certificate.json`, `channels.json` and both AO float files are
  byte-identical across MSVC, GCC 13.3 on Linux and the WebAssembly build, and
  from 0.5.1 so is `receipt.json`. CI compares the three builds' files on
  every push. It also checks the pixels, both AO files, `channels.json` and the
  certificate (apart from its version string) against the hashes 0.5.0 wrote.
  `frame.ppm` is also unchanged from 0.2.0.
- **WebAssembly matches native exactly.** For all five views below at
  512 x 512, the wasm build's eight output files hash the same as the native
  build's, so RMSE, maximum error, pixel count and verdict agree with zero
  difference. The comparison tolerance (RMSE within 1e-4, maximum error within
  1/64, identical pixels and verdict) was fixed before the first wasm run.
  Evidence: `evidence/wasm-vs-native-512.json`.
- **Thread count does not change output:** `--threads 1` and `--threads 8` give
  identical files.
- **The verdict depends on the view.** Same scene, 512 x 512, tolerance 0.12:

| View | Flags | Covered pixels | RMSE | Verdict |
|---|---|---|---|---|
| Default | none | 151,984 | 0.1349 | refuted |
| High | `--eye 0,9,3 --target 0,0.5,0` | 239,854 | 0.0827 | verified |
| Low | `--eye 6,1.5,2 --target 0,1,0` | 140,838 | 0.1659 | refuted |
| Close | `--eye 2,2.5,3 --target 0,0.8,0 --fovy 0.7` | 214,043 | 0.2167 | refuted |
| Wide | `--eye 5,5,8 --target 0,0.5,0 --fovy 1.2` | 88,869 | 0.0882 | verified |

- **Speed.** Median of 5 renders, in milliseconds: wall time for one full frame
  with no file output (`--bench 5`). "Full" includes the ray-traced reference.
  "No RT" is rasterization, screen-space AO and shading only. The wasm columns
  ran in a Worker in headless Chromium 154, and the threaded build used 24
  threads.

| Frame | Mode | wasm | wasm SIMD | wasm threads | native | native 24 threads |
|---|---|---|---|---|---|---|
| 256 x 256 | Full | 252 | 298 | 32 | 258 | 241 |
| 512 x 512 | Full | 1,013 | 1,143 | 127 | 1,028 | 293 |
| 1440 x 900 | Full | 4,646 | 4,881 | 523 | 4,527 | 613 |
| 256 x 256 | No RT | 27 | 31 | 5 | 29 | 114 |
| 512 x 512 | No RT | 110 | 116 | 21 | 118 | 119 |
| 1440 x 900 | No RT | 533 | 541 | 85 | 527 | 199 |

  Single-threaded wasm runs at native speed. SIMD gives no gain, because the
  hot loops (per-pixel hashing and ray-triangle tests) do not auto-vectorize.
  The threaded wasm build is the fastest configuration measured. Native
  threading starts fresh threads for every pass, and that cost shows at small
  frames; a persistent thread pool is the next step there. The machine was
  running other work during these runs (about 60% total CPU load), which
  affects the multi-threaded columns most. Raw runs: `evidence/bench-*.json`.

- **Serving the threaded build** needs `SharedArrayBuffer`, which browsers grant
  only to pages sent with `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp`. GitHub Pages cannot set those
  headers, so a Pages site can serve the single-threaded build and not the
  threaded one. `bench/serve.py` sets them for local runs.

Limits, stated plainly:

- The scene is one built-in test scene: a box on a ground plane under one
  directional light. There is no model loader yet.
- The ray-traced reference uses 64 hemisphere samples per pixel drawn from a
  deterministic per-pixel hash. It is a repeatable estimate of the integral and
  shows visible grain.
- The screen-space method is a simple one with 24 samples per pixel. It misses
  most of the occlusion the reference finds, so most views come back `refuted`.
  The check is reporting a weak shortcut there.
- A pass on RMSE says nothing about the worst pixel. The high view passes at
  0.0827 RMSE with a 0.625 maximum error.
- Byte-identical output was checked on x86-64 with two compilers and on
  WebAssembly. Other CPU architectures are untested.

## How the memory budget works

The CLI renders twice. The first pass runs inside a generous slab and records
the exact number of bytes the frame needs. The second pass renders again inside
a budget of exactly that many bytes, using a bump allocator that refuses any
request past the budget and never grows. If anything allocates more the
second time, the render stops and the memory certificate says `refuted`.

## Use it as a library

`cmake --install build --prefix <dir>` installs the CLI, the static library, the
headers, the license and a CMake package. Then, in your project:

```cmake
find_package(raw_native 0.5 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE raw_native::raw_native)
```

The package sets `raw_native_LICENSE` to `FSL-1.1-MIT`.

## Layout

```
raw/       headers: vectors, matrices, images, scene, G-buffer, rasterizer,
           ray-traced AO, SSAO, reconcile, certificate, composite, arena, motion
src/       implementation; src/gpu/ holds the GPU backends, the WGSL passes and the
           HLSL generated from them
app/       command-line driver
tests/     one test executable per test_*.cpp; tests/gpu/ holds the D3D12
           runtime test
wasm/      browser loader and a Node runner for the WebAssembly build
cmake/     WebAssembly, WebGPU and D3D12 build settings
scripts/   independent recheck, render comparison, timing, the WGSL-to-HLSL
           translator and release packing
bench/     the browser timing pages (CPU wasm and WebGPU) and a local server
evidence/  raw timing runs, the wasm-versus-native comparison and the GPU
           certificates (D3D12, WARP and WebGPU)
docs/      example images and certificates
third_party/superstack/  the vendored superstack header, its vectors and
           their pins (FSL-1.1-MIT; see NOTICE.md there)
```

## License

From version 0.3.0, raw-native is released under the Functional Source License,
Version 1.1, MIT Future License (`FSL-1.1-MIT`). See [LICENSE](LICENSE). You may
use, copy, modify and redistribute it for any purpose other than a competing
commercial product or service, and each version becomes available under the MIT
license two years after its release.

Version 0.2.0 and earlier remain under the MIT license, as released.

The files in `third_party/superstack/` are superstack 0.2.0, also under
FSL-1.1-MIT, with the notice carried in each file and in
`third_party/superstack/LICENSE.txt`. The public domain algorithms the header
contains keep their own terms. raw-native 0.5.0 vendored superstack 0.1.0,
which remains under the MIT license.

Copyright 2026 Zain Dana Harper.

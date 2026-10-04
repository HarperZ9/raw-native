# Changelog

Each release's full notes, with downloads and evidence, are on the
[releases page](https://github.com/HarperZ9/raw-native/releases). Dates are
the day the release was published on GitHub, in UTC.

## Unreleased

- **Web GPU host.** `web/raw-gpu.mjs` runs WGSL compute and render pipelines,
  the frame graph, per-pass GPU timing and canvas presentation on WebGPU, as
  one dependency-free ES module. `web/frame-graph.mjs` follows the C++ graph's
  rules and is tested against its golden output (ADR 0010).
- **Threads.** A creative module: particles flow along the zero set of fifteen
  form fields and leave trails tone-mapped by log density. One WGSL source runs
  on the web host (`web/threads.mjs`) and on D3D12 (`raw_native_cli threads`).
  It has no CPU reference and writes no certificate.
- **Threads compiles per world on the web.** The world code moved into the one
  pass that uses it, and the web host builds the advance pass for the one or
  two worlds on screen, on demand. Time to the first frame in Chrome on D3D12
  went from 24 s to about 0.3 to 1.3 s.
- **Translator.** `scripts/wgsl_to_hlsl.py` translates `fract`, `mix` and
  `atomicAdd` statements, refuses builtins it cannot map faithfully, and no
  longer treats a comparison inside a constructor as a template bracket, which
  had hidden the comma in `vec2f(a, select(b, c, k >= 2))`. The renderer's
  generated HLSL is unchanged.
- **Layered source tree.** Headers and sources move into layer directories
  (`math`, `core`, `cert`, `scene`, `rhi`, `graph`, `renderer`, `tools`), and
  `scripts/check_layers.py` checks in CI that each layer includes only the
  layers allowed for it. The 0.5 include paths (`raw/render.hpp` and the
  others) still work through one-line forwarders, removed one minor release
  later. See `docs/architecture/`.
- **Render hardware interface.** `raw/rhi/rhi.hpp` is the one boundary between
  the renderer and a graphics API. The D3D12 and WebGPU backends implement it;
  the GPU frame (buffers, passes, bindings, read-back) now exists once, in the
  renderer, where each backend used to carry its own copy.
- **Frame graph.** `raw/graph/frame_graph.hpp` validates each frame, culls
  passes whose results nothing reads and places barriers from declared
  accesses. The GPU renderer and the CPU reference both run through it.
  Frame-only GPU renders now skip the passes the frame does not need.
- **Binding layouts from the WGSL.** `scripts/wgsl_to_hlsl.py` writes each
  pass's bindings and their access into `pass_layout.hpp`; the graph's
  barriers and the D3D12 root signatures come from it.
- **Checks.** `RAW_NATIVE_D3D12_DEBUG=1` now fails a submission on any D3D12
  debug-layer error. `scripts/identity_matrix.py` hashes every file of eleven
  cases; CI compares MSVC and GCC against `evidence/identity-golden.json`.
- **Output unchanged.** Every certificate, receipt, channel summary and image
  is byte-identical to 0.5.1's, on every build and backend checked.

## 0.5.1

- **superstack 0.2.0.** The vendored `superstack.hpp` and its vector runner come
  from superstack's v0.2.0 tag, byte for byte. The header's SHA-256 is
  `a0d223c0567c883b73ed74b19d93cb087e213dc14b950ab0f6f04206cb72c590`.
  The vectors are unchanged.
- **The WebAssembly build writes `receipt.json`.** superstack 0.2.0's header
  compiles with libc++, so the wasm build no longer skips the receipt. Its
  bytes equal the receipts from MSVC on Windows and GCC on Linux, and
  `raw_native_cli verify` passes on it. CI requires both.
- **Licence of the vendored header.** superstack 0.2.0 is FSL-1.1-MIT, so
  `third_party/superstack/LICENSE.txt`, shipped in every archive as
  `LICENSE-superstack.txt`, moves from MIT to FSL-1.1-MIT. The notices for
  mulberry32, xmur3, OKLab and libebur128 are kept word for word.
- **CMake 4.2.0 is pinned for the wasm build.** CMake 4.2 adds `-fPIC` to
  Emscripten compiles, so another version gives different wasm bytes.
  `cmake/wasm.cmake` warns on any other version, and CI installs 4.2.0 and
  checks that the wasm build used it.
- **Output unchanged.** `certificate.json`, `channels.json`, the frame and the
  AO files hash the same as in 0.5.0, apart from the certificate's version
  string. CI checks them against 0.5.0's hashes.

## 0.5.0 (2026-10-04)

- A native D3D12 backend for Windows, built with `-DRAW_NATIVE_GPU_D3D12=ON`.
  Its HLSL is generated from the WGSL, so there is one shader source.
- `--gpu` refuses software adapters, including the Microsoft Basic Render
  Driver. `RAW_NATIVE_D3D12_WARP=1` selects WARP on purpose.
- Every render writes `receipt.json`, a `superstack.receipt/1` receipt that
  reports identity and the tolerance verdict separately. A `--gpu` run adds
  `gpu_receipt.json`. `raw_native_cli verify` checks both.
- `gpu_certificate.json` gains `identity`, both frame hashes and
  `adapter.driver`. `certificate.json` is unchanged apart from its version
  string.
- The wasm build writes no receipt: superstack 0.1.0's header does not
  compile with libc++.

## 0.4.0 (2026-10-03)

- A WebGPU backend in WGSL that runs the whole frame as compute passes.
- `--gpu` renders on the GPU and on the CPU, then writes
  `gpu_certificate.json` (`raw-gpu-cert/1`) comparing the two. The bounds in
  `raw/gpu_tolerance.hpp` were committed before any GPU code existed.
- The default native build and the CPU wasm build contain no GPU code. There,
  `--gpu` exits 4 with an `unverifiable` certificate.

## 0.3.0 (2026-10-03)

- Certificate schema `raw-cert/2`, with the renderer version, canonical
  params, sample counts and the SHA-256 of every output file.
- `ao_rt.pfm`, `ao_ss.pfm` and `mask.pgm`, so `raw_native_cli verify` and
  `scripts/recheck.py` can recompute RMSE and maximum error from the files.
- `--tolerance`, `--no-rt`, `--threads` and `--bench`.
- A WebAssembly build and a loader that checks both files against a SHA-256
  before running them.
- The licence changes to FSL-1.1-MIT. Version 0.2.0 and earlier stay MIT.

## 0.2.0 (2026-10-03)

- First public release: ray-traced and screen-space ambient occlusion, a
  reconcile step and `certificate.json` with a `verified` or `refuted` verdict.
- A bounded memory arena, recorded in `arena_certificate.json`.
- Camera and frame-size flags, a JSON params file and a previous camera for
  motion vectors.
- `frame_hdr.pfm` and `channels.json`.
- CMake install rules and a `raw_native` package for `find_package`.

# ADR 0010: the web GPU host, and creative modules

**Status:** Accepted. **Date:** 2026-10-04. **Amends:** [0008](0008-contracts-and-web.md).

## Context

The author's direction, 2026-10-04: "All of the engines should be in the
studio, and all should be rendered through the completed and optimized
raw-native engine." ADR 0008 kept raw-native as one plugin of the site's media
engine, a reference renderer for 3D scenes. The Studio now needs raw-native as
its GPU path: compute and render pipelines, a frame graph of passes, storage
buffers, ping-pong resources, timing and presentation on a canvas, called from
the Studio's own JavaScript once per animation frame.

raw-native already runs on WebGPU through the wasm build (`raw-native-gpu`,
Emscripten's WebGPU port with JSPI). That build blocks on GPU waits through
JSPI, and every WebGPU call crosses from wasm into JavaScript glue.

## Decision

**A JavaScript host for the web, the same shaders and the same graph rules.**

- `web/raw-gpu.mjs` is the RHI's object model on `navigator.gpu`: buffers,
  WGSL compute and render pipelines, cached bind groups, one command encoder
  and one submit per frame, per-pass GPU timing from timestamp queries, and
  presentation of a packed RGBA8 buffer on a canvas. No dependency.
- `web/frame-graph.mjs` implements `raw/graph/frame_graph.hpp` in JavaScript:
  the same validation, culling and barrier placement. Its test replays every
  case of `tests/test_frame_graph.cpp` and must give the same `describe()` text
  and the same event log. Imports may be functions, resolved per frame, for
  ping-pong buffers; "attachment" is a write access for a render target.
- A shader is written once, in WGSL, in the subset `scripts/wgsl_to_hlsl.py`
  translates. The web host runs it as written; the D3D12 build runs the
  generated HLSL. The translator now maps `fract`, `mix` and `atomicAdd`
  statements, and refuses the builtins it cannot map faithfully.
- **Creative modules** are render features with no CPU reference. Per ADR 0007
  they say so, and they write no certificate or receipt: the author's ruling
  for the Studio's creative paths is "This is simply creative media". The
  first is Threads (`shaders/threads.wgsl`, `raw/renderer/threads.hpp`,
  `web/threads.mjs`, `raw_native_cli threads`). The renderer's verified paths
  keep their certificates unchanged.

## Why JavaScript and not the wasm build

- JSPI ships in Chromium-based browsers only; a host built on it would not run
  in Safari or Firefox, which ship WebGPU.
- A frame of a Studio module is a handful of dispatches recorded every 16 ms.
  The cost that matters is GPU time and per-call overhead; a wasm host adds a
  crossing per WebGPU call and changes neither.
- The wasm build stays: it is still the CPU reference and the WebGPU
  certificate path in the browser.

## Consequences

- Two implementations of the graph rules, C++ and JavaScript, held together by
  one golden test. A rule change lands in both or CI fails.
- Modules are WGSL plus a small JavaScript driver; their native path is the
  same WGSL on D3D12 through the RHI.
- WebGPU is required for a module on the web. A page that must also run
  without WebGPU keeps its existing path as the fallback.

## Reversal signal

JSPI in every browser that ships WebGPU and a measured frame-time advantage for
a wasm host on a Studio module would reopen the choice.

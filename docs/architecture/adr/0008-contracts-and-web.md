# ADR 0008: superstack contracts and the site's JS engine

**Status:** Accepted. **Date:** 2026-10-04.

## Context

The author's work has three rendering surfaces: raw-native (C++ with
WebAssembly builds), the site's media engine (dependency-free JavaScript with a
scheduler, a plugin contract, a WebGL2 context pool and a sound layer), and
other producers in Python and JavaScript. superstack is the contract they
share: canonical JSON, a seed rule, a flick clock, two-verdict receipts
(identity and tolerance), colour and sound. Each engine vendors superstack's
single-file implementation for its language, pinned by hash, and runs the
shared vectors in its own CI.

The site engine already loads raw-native's wasm builds as a plugin and uses the
CPU build as the reference that other plugins are reconciled against. Two
futures were possible: raw-native becomes the site's engine (a web front end
over the wasm build), or the two stay separate engines that share contracts.

## Decision

**Separate engines, shared contracts.**

- The site engine stays a scheduler, a plugin host and a WebGL2 pool with no
  build step. raw-native does not take on page scheduling, DOM or motion
  policy.
- raw-native's WebAssembly builds stay one plugin of the site engine: loaded by
  `raw-loader.mjs`, checked against published SHA-256, used as the reference
  renderer for 3D scenes. The WebGPU build is the same plugin's fast path,
  reconciled against the CPU build in the browser.
- Both sides speak superstack. As raw-native grows:
  - the scene description gains `superstack.scene/1` import and export, so any
    producer hands raw-native a scene and gets a receipt back;
  - frame time is in flicks, shared with sound;
  - receipts move from `tools` to `cert` once they stop depending on CLI
    parameters ([ADR 0001](0001-layered-architecture.md));
  - the audio layer, if it comes, writes `superstack.sound/1` receipts against
    an offline sample-exact reference.
- **A web viewer** (load glTF, render on WebGPU, show the certificate) is the
  likely first consumer surface. It is built as a site-engine plugin over the
  wasm build, on this boundary, not as a second renderer in JavaScript.

## Consequences

- The site keeps its weight budget: raw-native's wasm loads only where a page
  asks for 3D.
- Two engines must keep agreeing on superstack. The shared vectors are how that
  stays true; a superstack version bump is a pull request in each engine.
- Features that both need (colour transforms, tone mapping curves) are defined
  once in superstack and implemented per language, checked by the vectors.

## Alternatives

- **raw-native as the site's only engine.** It would put a 3D renderer's wasm
  on every page, and the site's 2D and generative pieces gain nothing from it.
- **A JavaScript port of the renderer.** Two implementations of one renderer
  is the duplication superstack exists to prevent.

## What would reverse it

If most site pages come to need 3D rendering, a shared runtime starts to pay,
and raw-native's wasm build would become the site engine's core.

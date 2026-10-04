# ADR 0006: The CPU reference and the certificates as the engine grows

**Status:** Accepted. **Date:** 2026-10-04.

## Context

Today one scene renders on the CPU in well under a second at 512 x 512, every
GPU frame is reconciled against it, and the default render's bytes are
identical from MSVC, GCC and Emscripten. A consumer renderer brings large
scenes, textures, PBR, shadows, temporal effects, many GPUs and many drivers.
Each strains a different part of the verification story. This record sets how
each part survives.

## Decision

### 1. The CPU reference is an engine layer, and every feature lands in it

The reference lives in the `renderer` layer and runs as a host frame graph with
the same pass names as the GPU graph ([ADR 0003](0003-frame-graph.md)). A
render feature lands in the reference first or together with its fast path.
A feature that cannot have a bit-exact reference (texture filtering hardware,
for one) gets a reference with a published tolerance, the way the GPU
reconcile's bounds were committed before the first GPU run. A feature with no
reference at all reports `unverifiable`, never `verified`.

### 2. Determinism is scoped by device class, and the class is recorded

| Path | Promise | Checked by |
|---|---|---|
| CPU reference | Byte-identical across toolchains and platforms | `evidence/identity-golden.json` on MSVC and GCC every push; the wasm identity job |
| One GPU backend on one device class | Byte-identical run to run | A golden manifest per device class, recorded on a hardware runner |
| Across device classes | Tolerance verdict against the CPU reference | `raw-gpu-cert/1` on every GPU run |

A device class is backend, vendor, architecture and driver version, all of
which the GPU certificate already records. A golden for one class is never
applied to another.

Two known limits:

- `arena_certificate.json` differs between MSVC and libstdc++ (55 and 43
  allocations for the default render), because the two standard libraries
  grow vectors differently. It was never part of the cross-platform identity
  claim. Making it identical means arena-backed containers that do not use
  `std::vector`'s growth policy.
- The CPU path's identity relies on IEEE float arithmetic without contraction.
  x86-64 builds have no fused multiply-add unless asked for, so they are
  identical today. An ARM64 build would let GCC and Clang contract `a*b+c` into
  FMA under the GNU dialect, which CMake selects by default; before the first
  ARM64 build, the reference must compile with `-ffp-contract=off` (and MSVC's
  `/fp:precise`, its default). **[from memory, moderate confidence; to verify
  on the first ARM64 build]**

### 3. Certificates are the golden-image system

- `scripts/identity_matrix.py` renders a fixed matrix of cases and hashes every
  file each one writes, normalizing only the wall-clock fields.
- `evidence/identity-golden.json` is the CPU golden. CI compares MSVC and GCC
  against it on every push. Regenerating it is a deliberate act in a pull
  request whose description says why each hash moved.
- Every structural change runs the matrix before and after, on every backend
  available, and attaches the comparison.
- The matrix grows with the engine: glTF sample models become cases at M1, and
  each device class with a hardware runner gets its own GPU golden.

### 4. The reference stays fast enough to run

At 1440p with PBR, shadows and many lights, a full CPU reference frame may take
seconds to minutes. Three levers keep it usable, in order:

1. **Small frames in CI.** Most defects show at 256 x 256.
2. **Tiles.** Certify a seeded selection of tiles at full resolution. The tile
   choice comes from the superstack seed rule, so a checker can recompute which
   tiles were judged, and the certificate lists them.
3. **Threads.** The reference parallelizes by rows today with output identical
   for any thread count; that rule extends to tiles.

### 5. Checks prove they can fail

Every check ships a negative control: the layer check and the shader translator
have self-tests that feed them violations, the receipt verifier has flipped-byte
tests, and the D3D12 debug layer was shown to fail a submission on a dropped
transition. A new check without a control is incomplete.

### 6. Independent checkers

`scripts/recheck.py` re-derives the AO verdict from the files with no shared
code. Each new certificate family gets its own independent checker, and the
two must agree bit for bit on the recorded values.

## Consequences

- Features cost more to land: a reference and a certificate come with each.
  That is the product.
- CI time grows with the matrix. The CPU matrix takes about 24 s on a desktop
  CPU today; tiles and small frames keep it bounded.

## What would reverse it

A class of features where no reference, even a tolerance one, can be written.
Path-traced global illumination is the likely candidate; its reference would be
a converged offline render, checked statistically. That needs its own ADR.

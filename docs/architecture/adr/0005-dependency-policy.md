# ADR 0005: Dependency policy for a consumer engine

**Status:** Accepted. This is author decision A2. Accepted by the author on 2026-10-04 ("approve all six engine decisions as proposed"). **Date:** 2026-10-04.

## Context

raw-native's default build depends on the C++ standard library and one
vendored header (`superstack.hpp`, pinned by SHA-256, FSL-1.1-MIT). The GPU
builds add only platform SDKs: `d3d12.lib`, `dxgi.lib` and DXC from the Windows
SDK, and Emscripten's WebGPU port. That property is part of why the renderer's
output can be trusted: there is little code in the pixel path the project did
not write.

A consumer renderer cannot keep it whole:

- **Vulkan and Metal** need their SDKs and loaders. These are system
  libraries; the question is only how they are found and pinned.
- **Windowing, input and swapchains** need OS libraries (Win32, X11 or Wayland,
  Cocoa). A platform layer such as SDL3 wraps them; writing the same per OS is
  months of work that adds nothing to verification.
- **Asset formats** need parsers and decoders: glTF 2.0 (JSON plus binary
  buffers), PNG and JPEG, KTX2 with Basis Universal transcoding, and mesh
  optimization for LODs. Writing these is possible (glTF and PNG are
  well-specified) but each is a large, security-sensitive surface.
- **GPU memory** for large scenes needs a suballocator (D3D12MA, VMA), both MIT.
- **Shaders** with many permutations may need Slang ([ADR 0002](0002-rhi.md)).
- **Profiling and crash reporting** want Tracy and crashpad or sentry-native.

Godot shows a workable discipline: every library in `thirdparty/` has a README
entry with upstream URL, version or commit with date, licence and local
patches.

## Proposal

1. **Allowed:** platform SDKs and system libraries; and vetted libraries that
   are vendored, pinned and licence-checked, each behind an interface the
   engine owns.
2. **Never in the reference path:** the CPU renderer, the reconcile, the
   certificates, the receipts and the verifiers stay standard-library-only,
   plus superstack. A dependency may produce input (a decoded texture), never
   judge output.
3. **Vendored, not fetched.** Source lives in `third_party/<name>/` with a
   `SHA256SUMS` the CI verifies, as superstack does today. No package manager
   at build time, no network in the build.
4. **One manifest.** `third_party/MANIFEST.md` lists for each library: upstream
   URL, version or commit and date, SPDX licence, why it is here, which
   interface wraps it, local patches (ideally none), and who reviewed it.
5. **Licences:** MIT, BSD-2/3, Zlib, Apache-2.0, ISC and Boost are compatible
   with FSL-1.1-MIT distribution. Copyleft (GPL, LGPL, MPL for static linking)
   is excluded from the engine; it may appear in optional tools that ship
   separately. Each release archive carries every licence text.
6. **Behind an interface.** Every library is reached through one adapter file
   in its layer (for example `src/assets/gltf_cgltf.cpp` behind
   `raw/assets/gltf.hpp`). Swapping a library touches its adapter only, and the
   layer check confines its headers to that file.
7. **Optional where possible.** A library that only some builds need is an
   option off by default, as the GPU backends are today.
8. **Reviewed on update.** An update to a vendored library is its own pull
   request with the upstream diff summarized and the identity matrix run.

## Candidates, if the policy is accepted

From the research in [GAP-ANALYSIS.md](../GAP-ANALYSIS.md#sources); licences as
the upstream repositories state them on 2026-10-03, to be rechecked at vendoring.

| Need | Candidate | Licence | Alternative |
|---|---|---|---|
| glTF 2.0 parse | cgltf (single header) **[memory, unverified]** | MIT | write one: glTF is a JSON schema plus accessors |
| PNG and JPEG decode | stb_image **[memory, unverified]** | MIT or public domain | a PNG-only decoder of our own (zlib inflate plus filters) |
| KTX2 and Basis transcoding | Basis Universal | Apache-2.0 | ship uncompressed textures until needed |
| Mesh LOD | meshoptimizer **[memory, unverified]** | MIT | none needed before streaming |
| GPU memory | D3D12MA 3.2.0, VMA 3.4.0 | MIT | one committed resource per buffer (today) |
| Platform layer | SDL3 | Zlib **[memory, unverified]** | per-OS code in `src/platform/` |
| Shader compiler | Slang | Apache-2.0 with LLVM exception | the WGSL subset translator (today) |
| Profiling | Tracy | BSD-3 **[memory, unverified]** | timestamp queries and JSON |
| Crash reports | sentry-native with crashpad | MIT | none |

## Consequences if accepted

- The README's "no external dependencies" line becomes "no dependencies in the
  reference and certificate path; every other dependency vendored, pinned and
  listed".
- A supply-chain review becomes part of every dependency pull request.
- The CPU reference stays buildable with the standard library alone, so the
  oracle never depends on the code it checks.

## Alternatives

- **Keep zero dependencies.** Feasible for glTF and PNG, infeasible for Vulkan,
  Metal and a portable windowing layer without years of work. It would cede
  platform reach to every competitor.
- **A package manager (vcpkg, Conan, FetchContent).** Convenient, but it puts
  the network and someone else's version resolution in the build, and the
  release's bytes would depend on both.

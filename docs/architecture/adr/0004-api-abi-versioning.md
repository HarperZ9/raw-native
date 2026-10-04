# ADR 0004: Public API, ABI and versioning

**Status:** Accepted for 0.x. The 1.0 terms are **Proposed** (author decision
A6). **Date:** 2026-10-04.

## Context

raw-native is used three ways today: as a CLI, as a static library through
`find_package(raw_native)`, and as a WebAssembly module loaded by hash. A
consumer engine adds a fourth: binary plugins built separately from the engine.
Each needs a different promise. Mature engines differ here: Bevy breaks its
Rust API every release and ships a migration guide; Unreal's binary plugins
work with exactly one engine version; Godot's GDExtension is a C ABI where an
extension built for an older minor keeps loading in newer minors.

## Decision

**Surfaces and what each promises:**

| Surface | Boundary | Promise before 1.0 |
|---|---|---|
| C++ API | `raw/<layer>/*.hpp` | A minor release may break it, with notes in CHANGELOG.md. A moved header keeps a forwarder at its old path for one minor release |
| C++ ABI | none | No promise. Build the library with the same toolchain as its user |
| C ABI (plugins, planned) | one header of versioned function tables | A plugin built for minor N loads in every later minor of the same major |
| CLI | flags, exit codes, output file names | A flag or file is deprecated for one minor before removal |
| Certificate and receipt schemas | `schema` field | A schema version never changes shape; a new field is a new version |
| WebAssembly module | `raw-loader.mjs` and its exports | As the CLI; every release lists its wasm hashes |
| RHI | `kRhiVersion` | Bumped when a backend or caller must change |

**Private:** everything under `src/`, the generated pass layout and shader
blobs, and every backend's types.

**The 0.5 include paths** (`raw/render.hpp` and the other 27) forward to their
layered paths and carry a comment naming their removal: the minor release after
the one that introduced the layers. CI forbids engine code from using them.

**The C ABI, when it lands:** each table starts with its own size and version,
the engine fills only the entries the plugin's version knows, and new entries
only append. An `api.json` dump of the tables is committed, and CI checks a
change against the previous release's dump, as Godot's
`--validate-extension-api` does.

**1.0 (Proposed, A6):** promise source compatibility of the C++ API within a
major release, after the roadmap's M3 milestone, when the RHI has textures,
graphics pipelines and a second native backend. Promising earlier would freeze
an interface that has only seen compute.

## Consequences

- Downstream code written against 0.5 builds unchanged against the next minor
  and warns nowhere; it breaks one minor later. A compiler warning in the
  forwarders would help, but `#pragma message` is not portable across MSVC,
  GCC and Clang without macros, and the forwarders are only one line.
- No ABI promise means no binary distribution of the library for third-party
  linking. The CLI and wasm module are the binary surfaces.

## What would reverse it

A real external consumer who needs ABI stability before 1.0. The answer would
be to publish the C ABI early. The C++ ABI stays without a promise.

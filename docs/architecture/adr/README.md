# Architecture decision records

Each record states one decision, the context that forced it, what it costs and
how we would learn it was wrong. A record is **Accepted** when it describes
code that exists or a rule CI enforces, and **Proposed** when it waits on the
author. A record is never edited to say something else; a later record
supersedes it and both stay.

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-layered-architecture.md) | Layers, and the include rules CI enforces between them | Accepted |
| [0002](0002-rhi.md) | A render hardware interface modelled on WebGPU, with barriers from the frame graph | Accepted, including backend order and shader language (A1, A5) |
| [0003](0003-frame-graph.md) | One frame graph for the GPU renderer and the CPU reference | Accepted |
| [0004](0004-api-abi-versioning.md) | Public API, ABI and versioning policy | Accepted, including 1.0 terms (A6) |
| [0005](0005-dependency-policy.md) | Dependency policy for a consumer engine | Accepted (A2) |
| [0006](0006-verification-at-scale.md) | The CPU reference and the certificates as the engine grows | Accepted |
| [0007](0007-plugin-model.md) | Plugin model | Proposed |
| [0008](0008-contracts-and-web.md) | superstack contracts and the site's JS engine | Accepted |
| [0009](0009-scope.md) | Product scope: renderer or engine, and the editor | Accepted (A3, A4) |
| [0010](0010-web-host.md) | The web GPU host in JavaScript, and creative modules without a CPU reference | Accepted |
| [0011](0011-motion.md) | Motion: a scene and timeline layer for mathematical animation, live and offline | Accepted |
| [0012](0012-media-engine.md) | The media engine: per-repo scene specs read from the release, one render command, a release workflow split between CI and the author's machine | Accepted |
| [0013](0013-media-grade-roadmap.md) | The media-grade roadmap (M1 to M5) and five decisions: A5 reading, ffmpeg external, no MPL muxer, no binary-only upscalers | Accepted |
| [0014](0014-game-runtime.md) | A game runtime in scope, staged G0 to G4, and M6 authoring (amends A3) | Accepted |
| [0015](0015-api-snapshot.md) | A versioned API snapshot at M3; the freeze waits for a complete engine (amends A6) | Accepted |
| [0016](0016-style-templates.md) | Three starter templates as the style acceptance corpus | Accepted |

To add one, copy the shape of an existing record: status and date, context,
decision, consequences, alternatives, and the signal that would reverse it.

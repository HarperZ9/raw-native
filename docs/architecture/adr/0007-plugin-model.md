# ADR 0007: Plugin model

**Status:** Proposed. **Date:** 2026-10-04.

## Context

A consumer engine is extended by people other than its author: importers for
new formats, render features, tools. The site's media engine already has a
plugin contract that works for its pages
(`{ id, version, backends, create(...) }` returning `{ frame, setParams?,
resize?, readPixels?, dispose }`). Bevy plugins are compiled-in Rust; Unreal
plugins are modules with a descriptor and a loading phase, and binaries tied to
one engine version; Godot's GDExtension is a versioned C ABI.

## Proposal

**Four kinds of plugin,** each with one extension point:

| Kind | Extension point | Lands with |
|---|---|---|
| Importer | `assets`: reads a format into the scene description | M1 (glTF) |
| Render feature | `renderer`: adds passes and their shaders to the frame graph | M2 |
| RHI backend | `rhi`: implements the device interface | M3 (Vulkan) |
| Tool command | `tools`: a CLI verb or a viewer panel | M1 |

**Every plugin declares:** an id, a version, the API version it was built
against, its kind and capabilities, and its verification:

- an importer declares a round-trip test corpus;
- a render feature declares its CPU reference implementation and its
  tolerances, or declares that it has none, in which case its outputs are
  certified `unverifiable` for identity;
- a backend passes the identity matrix on its device classes.

**Two delivery forms:**

1. **Compile-time** (first): a static registration table per extension point,
   filled by each plugin's translation unit. All first-party features use this.
2. **Binary** (after the C ABI exists, [ADR 0004](0004-api-abi-versioning.md)):
   a shared library exporting one entry point that returns a versioned table.
   A binary plugin that adds a render feature runs its CPU reference in the
   host process, so a certificate never depends on unverified plugin code
   judging its own output.

**Receipts name plugins.** A frame's receipt lists every plugin that touched it
with id, version and the SHA-256 of its binary or source, so a verdict can be
traced to the code that produced it.

## Consequences

- No plugin can mark its own output verified; the host's reference and
  reconcile do that.
- The registration tables are the first consumers of the layer rules: a
  plugin's code lives in the layer of its extension point.

## Alternatives

- **Scripting-language plugins first** (Lua, JavaScript). Lower barrier, but it
  puts an interpreter in the engine before scope decision A3.
- **Unreal-style modules with per-version binaries.** Rejected: every release
  would orphan every binary plugin.

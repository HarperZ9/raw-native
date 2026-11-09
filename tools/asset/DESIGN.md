<!-- writing-profile: research -->
# Asset toolchain: design and bounds

2026-10-22. First slice. Author direction, 2026-10-23, verbatim: Low Water's characters and bosses are "All
hand-built" (D1), and "Ideally we should not need blender, and will build our own assets." The tool is
reusable across games and the engine. Shipped tools carry no dependencies on outside code.

## 1. Where it lives

**Decision: `tools/asset` inside raw-native.** Plain Node ESM, standard library only, run with
`node tools/asset/bin/asset.mjs`.

Reasons, in order of weight:

1. The engine already owns the formats the tool exports to (glTF through `src/assets/gltf.cpp`) and the
   shading vocabulary the review renderer imitates. A tool beside them changes with them in one commit.
2. Games consume the tool by path or release, and keep their own assets and adapters. Low Water holds
   `characters/assets/heads/ned.sculpt.json` and its adapter. The tool holds nothing game specific.
3. A new repository would need its own release, CI and versioning for about two thousand lines. That cost
   buys nothing until a second engine wants the tool without the renderer.

Revisit when a consumer outside raw-native exists. The modules have no imports outside `tools/asset`, so
the move is a directory copy.

## 2. Pipeline

```
 name.sculpt.json  (strokes: the source of truth, one a line, diffable)
        |  sdf.mjs        ordered fold of strokes into a signed distance field
        v
   field  --> extract.mjs   surface nets on a sparse brick grid, vertices projected onto the zero set
        |         |
        |         +--> reference mesh (1.5 mm), used only to measure error
        |  retopo.mjs     coarse quad net, tangential relaxation, re-projection (3 LODs)
        v
  game quad meshes --> uv.mjs     charts by direction, one texel density, shelf packing
        |  bake.mjs      each texel: Newton to the sculpt surface, then normal, curvature, cavity,
        |                ambient occlusion, thickness
        v
  name.paint.json --> paint.mjs   layers of zones, dabs, bands, shade, ink and grain, in 3D
        v
   albedo + maps + per-vertex colour --> render.mjs (review sheets) and export.mjs (glTF, OBJ, RNA1)
```

Every stage reads the field or its outputs. No stage reads a mesh from another tool.

## 3. Choices

**Sculpt representation.** An ordered list of brush strokes, each a primitive (ellipsoid, round cone,
rounded box, torus) with an operation (add, subtract, intersect), a smoothing width `k` in metres and an
optional mirror across x = 0. The field is a left fold over the list, so stroke order matters the way it does
under a sculptor's hand. Strokes tagged `hair` fold in their own field and join the skin with a small smooth
union, so a hair clip can never cut skin. Edits are inserts, moves and deletes of one line.

Chosen over a multires mesh because the file is small, text, diffable and resolution independent, and because
every later stage can ask the field for an exact distance. The cost is that organic detail has to be built
from primitives; fine skin detail (pores, wrinkles) needs a displacement stroke kind, which is future work.

**Mesh extraction.** Surface nets on a sparse grid of 8 by 8 by 8 bricks. A brick is skipped when no add
stroke can reach it, or when its centre distance exceeds 2.5 times its half diagonal. Each cell vertex is then
projected onto the zero set with Newton steps. Chosen over dual contouring because surface nets give quads
directly and stay manifold on thin lids; the projection recovers most of what the quadratic error function
would. Sharp creases round off at the cell size, which the error bound states.

**Quad retopology, version 1.** The game mesh is the same extractor at a coarser cell, relaxed tangentially
and re-projected. Output is all quads with valence 4 inside. It does not yet follow anatomical loops around
the eyes and mouth. Guide curves are the next step (section 9). Until then the guarantee is the error bound and
the quad quality numbers, both measured on every build.

**UVs.** Six directional charts (front, back, left, right, top, bottom), small charts folded into the
neighbour they touch most, planar projection at one global texel density, shelf packing with binary search on
the density. Vertices split at seams. Stretch of every triangle against the ideal density is measured.

**Baking.** There is no high poly mesh. Each texel takes its game surface point, walks to the sculpt surface
by Newton steps, and reads the normal and curvature exactly from the field. Ambient occlusion (24 cosine
distributed rays, rotated per texel by a hash) and thickness (march inward until the field turns positive)
read a 2.5 mm distance volume. Normal maps are tangent space, X = +u, Y = +v with v down the image.

**Painting, the Oil and Ink way.** A paint file is layers of strokes laid in the sculpt's frame: ramps (the
forehead paler where a hat sits, the jaw cooler), dabs (cheek warmth, lips, brows), bands, a shadow lean that
turns occlusion toward violet, a thin-skin warm term from thickness, ink from cavity and curvature, and a
grain of noise stretched along a flow direction. The layer stack is a pure function of the surface sample, so
it paints texels and mesh vertices with one code path and the two cannot drift.

**Portrait render path.** `render.mjs` is an orthographic rasteriser with a warm key, a violet fill, light
quantised to painted value steps and ink on silhouettes. It reads the baked textures, so a review sheet shows
what the maps hold. The game's own figure pipeline remains the final check (section 7).

**File formats.**

| File | Content | Notes |
|:--|:--|:--|
| `*.sculpt.json` | header, eyes, strokes | numbers rounded to 1 micrometre, fixed key order, one stroke a line |
| `*.paint.json` | layers of paint strokes | display colour values in 0..1 |
| `*.rna` (RNA1) | LOD meshes, quads, per-vertex colour, curvature, cavity, region, UV mesh, eye parts | JSON header plus aligned typed arrays |
| `*.gltf` | portrait LOD with albedo and normal textures embedded | the form raw-native's loader reads |
| `*.obj` | portrait LOD with UVs | for any other tool |
| `bounds.json` | measured values for the build | compared with section 5 |

**Export.** raw-native reads the glTF directly. The Low Water browser mesh (`.lwmesh`, magic LWM1) is written
by Low Water's own adapter from the RNA1 arrays, because material ids and joint weights belong to the game.

**Authoring UI.** Not built in this slice. The planned UI is a browser page over the same modules (all pure
ESM): orbit view with the field's mesh, a stroke list, handles that edit one stroke line, and live review
renders. Until then strokes are authored in the file format and edited through `src/format.mjs` and the
`Field` class, and `asset review` renders the four-panel sheet after each pass.

## 4. Bounds, written before the Ned build was measured

Units are metres unless stated. "Error" is the field's absolute distance at quad centres and edge midpoints,
where a net sags most, so it is the largest honest deviation from the sculpt.

| Quantity | Bound |
|:--|:--|
| Reference extraction at 1.5 mm | error p99 at most 0.15 mm, max at most 0.6 mm |
| Portrait LOD, 3 mm cell | error p99 at most 0.5 mm, max at most 1.5 mm; at most 40 000 triangles |
| Play LOD, 6 mm cell | error p99 at most 1.5 mm, max at most 4 mm; at most 9 000 triangles |
| Far LOD, 10 mm cell | error p99 at most 3 mm; at most 3 000 triangles |
| Quad quality, portrait | all quads; 5th percentile interior angle at least 55 degrees; aspect p95 at most 1.6 |
| UV stretch, portrait | p95 at most 1.8 against the ideal density; p99 at most 4 |
| Bake | no cage violation (a texel moving more than 10 mm), residual to the zero set at most 0.5 mm, normal map 8 bit round trip at most 1 degree |
| Timing, Node 25 on the author's workstation | reference extraction at most 20 s, whole quick build (512 maps) at most 90 s, whole full build at most 180 s |
| Determinism | two builds from the same files give identical RNA1 bytes and identical textures |
| Play size | the head at 300 pixels a metre is at most 9 000 triangles and reads as a head with eyes, nose, mouth and hairline |

The tests in `test/asset.test.mjs` assert the error, determinism, round trip and UV rows on synthetic
sculpts. Controls that must fail are in the same file: a bad stroke file, a 6 mm skewed sphere, and a build with
the sparse and cull paths turned off that must match the fast path.

## 5. What a pass does not prove

- The error bound is against the field. It says nothing about whether the field is a good face. Judging that is
  what the review sheets and the side by side capture are for, and a person has to look.
- Ellipsoid distances are first order. The brick skip uses a 2.5 times margin, and a test compares the sparse
  path with a dense uncull path on a head-like sculpt. A sculpt with extreme ellipsoid ratios could still
  defeat the margin.
- Ambient occlusion reads a 2.5 mm volume, so creases finer than that come from the exact cavity term.

## 6. Cost model

Reference extraction at 1.5 mm evaluates about two million field points after brick skipping. Texel baking
costs about fourteen exact field evaluations plus the ray marches. Builds are single threaded.

## 7. Verification path in a game

A game adapter loads the RNA1 file, places the head at its own joint, maps material names to its material
ids and renders through its own figure pipeline. Low Water's adapter is `characters/forge/authored.mjs` and the
comparison script is `characters/proto/head_compare.mjs`.

## 8. Known gaps

1. No anatomical edge flow in the game mesh.
2. No displacement stroke kind for pores and wrinkles.
3. Eyes are generated parts (sphere, cornea cap, pupil, catchlight) described in the sculpt header.
4. Hair is solid geometry. Cards and strands are not authored.
5. Skeleton weights and blend shapes (expressions) are not part of the format yet.
6. The authoring UI is specified and not built.

## 9. Toward a production toolchain

1. Guide curves for edge loops (eyes, mouth, nasolabial), then a quad flow solver constrained by them.
2. A displacement and a smoothing stroke kind, and stroke groups with names.
3. Texture painting by brush in the UI, with the layer file as the saved form.
4. Expression targets as stroke deltas, baked to blend shapes.
5. A full body: same format, strokes in a body frame, with a skeleton binding file.
6. A C++ RNA1 reader in raw-native so the engine loads the native format with the glTF as a fallback.
7. GPU extraction in WGSL once the CPU reference bounds are stable (the field and the brick skip port
   directly, and the CPU version is the reference).

## 10. SDF kit: primitives and modifiers (bounds written before the code)

Clean room. The kit takes the idea of exact signed distance geometry with a modifier stack from the
Shadytracer post (ideas only, no code). Published distance functions are cited in the code at the line that uses
them (Inigo Quilez, iquilezles.org/articles/distfunctions and /smin). Everything else is derived here.

Contract for every stroke kind. `d(p)` is a signed distance (negative inside). "Exact" means equal to the true
signed distance. "Bounded" means `|d| <= |d_true|` everywhere (safe for sphere tracing). The gradient is the unit
outward normal on the surface; for exact kinds it is the true gradient everywhere.

| Kind | Distance | Normal | Bound tested against a dense surface sample |
|:--|:--|:--|:--|
| `rbox` rounded box (half extents, round radius, rot) | exact | analytic | max error at most 1 sample spacing |
| `rcone` round cone (a, b, radii r0 at a, r1 at b; r0 = r1 is a capsule) | exact | analytic | same |
| `cone` frustum with flat caps (a, b, r0, r1) | exact (revolved convex polygon) | analytic | same |
| `torx` torus (R, r) | exact | analytic | same |
| `ext` extruded 2D profile (`rrect`, `circle`, `poly`; half length h) | exact | analytic | same |
| `ellb` ellipsoid | first-order estimate clamped into [lower, upper] | analytic on the surface | lower <= true <= upper at every query |

Ellipsoid bound. With k0 = |p / r|, the field `(k0 - 1) * min(r)` is a proven lower bound on the distance (the
scaling map p / r is `1 / min(r)` Lipschitz). The radial hit gives the upper bound `|p| (1 - 1 / k0)` outside and
`|p| (1 / k0 - 1)` inside. The kind returns the first-order estimate `k0 (k0 - 1) / k1` clamped into that
interval (a guard: 60 000 sampled points never left it), and `ellipsoidLower` exports the lower bound for conservative marching.

| Operation | Distance | Bound |
|:--|:--|:--|
| smooth union, quadratic (`blend: "quad"`, default) | `min(a, b) - h^2 k / 4`, h = max(k - abs(a - b), 0) / k | at most `k / 4` below `min(a, b)`; C1 |
| smooth union, cubic (`blend: "cubic"`) | `min(a, b) - h^3 k / 6` | at most `k / 6` below; C2, which is what removes the shading creases at blends |
| smooth subtraction | smooth max with the same k | same |
| `round r` | `d - r` | exact outside a convex shape, bounded inside |
| `shell t` | `abs(d) - t / 2` | bounded |
| `mirror` (axis, offset) | field of `abs(x)` | exact when the shape lies on one side of the plane |
| `repeat` (period, n each side) | clamped lattice | exact when the shape fits in half a period |
| `twist` (k rad/m about y, radius R) | `d / s`, s = (a + sqrt(a^2 + 4)) / 2, a = k R | bounded; s is the largest singular value of the warp Jacobian within R of the axis |
| `bend` (k rad/m of x, extent E) | `d / s`, a = k E | bounded, same s |
| `displace` (noise, ridge or map source, amplitude A, optional mask) | `(d + A m h) / (1 + A (L_h + L_m))` | bounded; L_h is the source's stated Lipschitz constant, L_m the mask's |
| `relief` (fbm of value noise) | displace with `L_h = sum a_i 6.5 f_i / sum a_i` | bounded; the 6.5 f bound on one octave is checked by sampling |

Tests assert the exact kinds against a dense surface sample, the bound intervals, analytic normals against
central differences, each modifier's bound by sampling, and controls that must fail (a deliberately wrong
Lipschitz constant, and a lower bound built on max(r) leaving the interval).

Extraction. Curvature-adaptive density is edge-collapse decimation of the fine surface net: collapses are ordered
by quadric error (flat regions go first), each collapsed vertex is re-projected onto the zero set, a collapse is
rejected if the field distance at the affected edge midpoints exceeds the LOD error bound, UV seams are locked, and
the loop stops at the triangle budget. Budgets: portrait 40 000, play 9 000, far 3 000 triangles inside the
section 4 error bounds. If a budget cannot be met inside the bound the build reports that and does not exceed the bound.

## 11. Landmark fit and eye seats (bounds written before the code)

A character gets a landmark set (front and profile) from anthropometric proportions, and `fit` optimises named stroke parameters
until the skin surface passes through the landmarks. An art-direction pass then adds character on top. Proportion sources are
cited in `src/anthro.mjs` (ANSUR II, Farkas, Loomis) with a confidence label on each; the digits are the place to correct.

| Quantity | Bound |
|:--|:--|
| Landmark error after the fit (distance from the landmark to the skin surface, Newton projection) | at most 2 mm at every landmark |
| Crease check | the p99 of the angle between neighbouring vertex normals of a 4 mm extraction rises by at most `creaseSlack` degrees (default 15) against the start; a failed check retries with a stronger trust weight, up to four attempts |
| Parameter limits | radii stay within 0.6 to 1.6 times their start; one step moves a parameter at most 10 mm |
| Result | the report states `tolMet` and the crease numbers whether or not the bound held; a missed landmark is listed, never hidden |

Eye seats. An eye record may carry `seat: { stroke, gap, offset }`. The globe then takes the centre of that lid stroke and a
radius `gap` short of its smallest radius, so moving the lid moves the eye.

What a fit does not prove. It places the surface at the landmarks. It does not make a face. The first Ned fit met the bound
at all but the jaw angle and still read worse than the generated head, because 22 landmarks leave most parameters free.

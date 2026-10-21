- **CPU skinning and morph reference** (`raw/scene/skin.hpp`, ROADMAP M4 criterion 3 step 1).
  A C++ port of the JavaScript oracle in `web/anim/skeleton.mjs`: skeletons with rest frames
  and poses, linear-blend skinning with four influences a vertex, and glTF 2.0 morph targets
  applied before skinning, in float64 with the oracle's operation order. On a 12-joint,
  400-vertex fixture written by the oracle (`tests/data/skin-fixture.txt`, kept in step by a
  node test), positions and normals match the oracle exactly over three poses (bound 2e-7).
  Two false controls miss the bound by orders of magnitude. GPU skinning and the glTF skin
  import are later steps. Bounds: `evidence/m4-skinning-cpu-bounds.json`.

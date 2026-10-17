#!/usr/bin/env bash
# M3 criterion 3 corpus (evidence/m3-aliasing-validation-bounds.json, checks a2, a3 and v3): the GPU
# renderer through the quick identity matrix and the ten glTF models, the post stack, the style
# passes, the texture identity and RHI v3 raster identity, written to OUT. The caller sets the
# environment (RAW_NATIVE_GRAPH_ALIAS, RAW_NATIVE_GRAPH_STATS, RAW_NATIVE_RHI_VALIDATE) and compares
# two OUT directories with m3_graph_compare.sh. Commands whose recorded misses exit non-zero still
# write their JSON; the comparison is what this corpus checks.
#   m3_graph_corpus.sh CLI OUT MODELS FIXTURES
set -u
cli=$1; out=$2; models=$3; fixtures=$4
mkdir -p "$out"
py=$(command -v python3 || command -v python)
"$py" scripts/identity_matrix.py run "$cli" "$out/quick.json" "$out/quick" --gpu --quick > "$out/quick.log" 2>&1; echo "quick exit $?"
"$py" scripts/identity_matrix.py run "$cli" "$out/gltf.json" "$out/gltf" --gpu --models "$models" > "$out/gltf.log" 2>&1; echo "gltf exit $?"
"$cli" post-parity --out "$out/post.json" > "$out/post.log" 2>&1; echo "post-parity exit $?"
"$cli" style-parity --fixtures "$fixtures" --out "$out/style.json" > "$out/style.log" 2>&1; echo "style-parity exit $?"
"$cli" texture-identity > "$out/texture.json" 2> "$out/texture.log"; echo "texture-identity exit $?"
"$cli" raster-identity --out "$out/rhi3.json" > "$out/rhi3.log" 2>&1; echo "raster-identity exit $?"
grep -h "rhi validation" "$out"/*.log > "$out/validation-reports.txt"
echo "validation reports: $(grep -c . "$out/validation-reports.txt")"
exit 0

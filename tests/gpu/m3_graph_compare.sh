#!/usr/bin/env bash
# Compare two runs of m3_graph_corpus.sh (M3 criterion 3): every output identical, wall-clock
# fields aside. Exit 0 only when all are.
#   m3_graph_compare.sh A B
set -u
a=$1; b=$2; bad=0
py=$(command -v python3 || command -v python)
for m in quick gltf; do "$py" scripts/identity_matrix.py compare "$a/$m.json" "$b/$m.json" || bad=1; done
for j in post style texture rhi3; do "$py" tests/gpu/m3_graph_gate.py same "$a/$j.json" "$b/$j.json" || bad=1; done
exit $bad

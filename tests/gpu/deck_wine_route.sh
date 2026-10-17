#!/usr/bin/env bash
# N3 criterion 5 (evidence/n3-deck-class-bounds.json): run the Windows D3D12 build under Wine with the
# DLL overrides already in WINEDLLOVERRIDES, and write OUT/LABEL-summary.json with one entry per check
# (holds true or false, and the last lines of its log). It never fails on a check; the caller judges.
#   deck_wine_route.sh LABEL OUT BIN WARP_LIST
# BIN holds raw_native_cli.exe and raw_native_present.exe; WARP_LIST is the WARP offscreen list at
# 1280 x 800; assets/raw-hero.gltf must exist (export-assets).
set -u
label=$1; out=$2; bin=$3; warp=$4
g=tests/gpu/present_gate.py; d=tests/gpu/deck_gate.py
mkdir -p "$out"
# Each Wine run is capped at 15 minutes: an exploration once hung for two hours with no output.
cli="timeout 900 wine $bin/raw_native_cli.exe"; pres="timeout 900 wine $bin/raw_native_present.exe"
declare -A holds
check(){ # name, command...: run, keep the log, record whether it exited 0
    local name=$1; shift
    "$@" > "$out/$label-$name.log" 2>&1
    local rc=$?
    holds[$name]=$([ $rc = 0 ] && echo True || echo False)
    echo "$label $name: exit $rc"; tail -4 "$out/$label-$name.log"
}
cert(){ # dir: gate a certificate the run wrote
    local dir=$1; shift
    cp "$dir/gpu_certificate.json" "$out/$label-$dir-gpu_certificate.json" 2>/dev/null
    python3 $d scene "$out/$label-$dir-gpu_certificate.json" --backend d3d12 --size 1280x800 --not-adapter microsoft "$@"
}
run_hero(){ $cli --model assets/raw-hero.gltf --gpu --no-rt --width 1280 --height 800 --threads 4 --out hero-$label && cert hero-$label --rt no; }
run_scene(){ $cli --gpu --width 1280 --height 800 --threads 4 --out scene-$label && cert scene-$label --rt yes; }
run_frames(){
    $pres run --frames 600 --width 1280 --height 800 --out "$out/$label-run1.json" &&
    $pres run --frames 600 --width 1280 --height 800 --out "$out/$label-run2.json" &&
    $pres run --frames 600 --width 1280 --height 800 --offscreen --out "$out/$label-offscreen.json" &&
    python3 $g same "$out/$label-run1.json" "$out/$label-run2.json" "$out/$label-offscreen.json" "$warp" --size 1280x800
}
run_controls(){
    # The 1280 x 720 run goes first so that, when no device opens, this log names the cause too.
    $pres run --frames 600 --width 1280 --height 720 --out "$out/$label-control-720.json" || return 1
    test -f "$out/$label-offscreen.json" || { echo "no reference list"; return 1; }
    if python3 $g same "$out/$label-control-720.json" "$out/$label-offscreen.json" --size 1280x800; then echo "size control passed: the gate is blind"; return 1; fi
    $pres run --frames 600 --width 1280 --height 800 --control --out "$out/$label-control-offset.json" &&
    python3 $g control "$out/$label-offscreen.json" "$out/$label-control-offset.json"
}
run_pacing(){
    $pres pacing --frames 600 --warmup 30 --width 1280 --height 800 --out "$out/$label-pacing.json" &&
    python3 $g pacing "$out/$label-pacing.json" --harness
}
check d3_hero run_hero
check d3b_scene run_scene
check d4_frames run_frames
check d5_controls run_controls
check d6_pacing run_pacing
python3 - "$out" "$label" d3_hero d3b_scene d4_frames d5_controls d6_pacing <<PY
import json, sys
out, label, names = sys.argv[1], sys.argv[2], sys.argv[3:]
holds = {$(for k in "${!holds[@]}"; do printf '"%s": %s, ' "$k" "${holds[$k]}"; done)}
summary = {"route": label, "wineprefix_overrides": "${WINEDLLOVERRIDES:-}", "checks": {}}
for n in names:
    try:
        tail = open(f"{out}/{label}-{n}.log", encoding="utf-8", errors="replace").read().splitlines()[-12:]
    except OSError:
        tail = []
    summary["checks"][n] = {"holds": bool(holds.get(n, False)), "log_tail": tail}
json.dump(summary, open(f"{out}/{label}-summary.json", "w"), indent=1)
print(json.dumps({n: summary["checks"][n]["holds"] for n in names}))
PY
exit 0

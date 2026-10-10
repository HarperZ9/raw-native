"""The M3 checks read no third-party model (author, 2026-10-10: "No test models, only full owned
assets."; bound "external_paths" in evidence/m3-assets-bounds.json).

    python scripts/check_owned_assets.py [--selftest]

Scans the M3 tests, GPU drivers and commands, and every CI step whose name marks it as M3, for a
path into a glTF-Sample-Assets checkout, a Khronos model name, a --models flag or a model fetch.
The M2 corpus (identity_matrix.py, gpu_models.py) belongs to M2 and is not scanned. Exit 1 on
any hit, naming it.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
M3_FILES = [
    "tests/test_shadows.cpp", "tests/test_post.cpp", "tests/test_taa.cpp", "tests/test_owned_assets.cpp",
    "tests/gpu/shadow_parity.cpp", "tests/gpu/post_parity.cpp", "tests/gpu/raster_identity.cpp",
    "src/tools/shadow_cmd.cpp", "src/tools/post_cmd.cpp", "src/tools/raster_cmd.cpp",
    "src/renderer/gpu/shadow_parity.cpp", "src/renderer/gpu/post_parity.cpp", "evidence/m3-scene-models.json",
]
BAD = re.compile(r"glTF-Sample-Assets|gltf-models|Models/[A-Z]|Suzanne|FlightHelmet|DamagedHelmet|SciFiHelmet|Sponza[/.]|ABeautifulGame|"
                 r"MetalRoughSpheres|--models|fetch_gltf|RAW_NATIVE_MODELS")


def scan_text(name, text):
    return [f"{name}:{i}: {line.strip()[:120]}" for i, line in enumerate(text.splitlines(), 1) if BAD.search(line)]


def m3_steps(ci_text):
    """The run blocks of CI steps whose name contains 'M3'."""
    out, current, inside = [], [], False
    for line in ci_text.splitlines():
        if re.match(r"\s*- name:", line):
            if inside:
                out.append("\n".join(current))
            inside, current = "M3" in line, [line]
        elif inside:
            current.append(line)
    if inside:
        out.append("\n".join(current))
    return out


def main(argv):
    if "--selftest" in argv:
        assert scan_text("x", "p.model = models + \"/Models/Suzanne/glTF/Suzanne.gltf\";")
        assert not scan_text("x", "scenes.push_back(owned::inTestScene(owned::hero(), 256, 256));")
        steps = m3_steps("      - name: a (M3)\n        run: python tools/assets/fetch_gltf.py gltf-models\n      - name: b (M2)\n        run: fetch_gltf")
        assert len(steps) == 1 and scan_text("ci", steps[0])
        print("check_owned_assets: selftest passed")
        return 0
    hits = []
    for f in M3_FILES:
        p = ROOT / f
        if p.exists():
            hits += scan_text(f, p.read_text(encoding="utf-8"))
    for k, step in enumerate(m3_steps((ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8"))):
        hits += scan_text(f"ci.yml M3 step {k + 1}", step)
    for h in hits:
        print(h)
    print(f"check_owned_assets: {len(hits)} third-party model reference(s) in M3 checks")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

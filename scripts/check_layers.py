#!/usr/bin/env python3
"""Enforce the layer rules of docs/architecture/ARCHITECTURE.md (stdlib only).

Every source file belongs to the layer named by its first directory under raw/
or src/ (app/ is the tools layer). A file may include:
  - its own layer, and the layers LAYERS lists for it (direct includes only);
  - a private header of its own layer by a relative path;
  - a graphics or OS API header only where API_HEADERS allows it;
  - a vendored header only where VENDORED allows it.
Public headers (raw/) include public headers only. Code includes the layered
path raw/<layer>/<name>.hpp, never the deprecated forwarders raw/<name>.hpp.
tests/ may include any public header and is not checked further.

Usage: check_layers.py [--selftest] [--report]
  exit 0 when every include is allowed, 1 on any violation.
  --report prints the layer-to-layer edges the code uses today.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Allowed direct dependencies. A layer always includes itself. Layers with no
# code yet are listed so their rules exist before their first file does.
LAYERS = {
    "math": [],
    "core": ["math"],
    "platform": ["math", "core"],
    "cert": ["math", "core"],
    "scene": ["math", "core"],
    "assets": ["math", "core", "scene"],
    "rhi": ["math", "core", "platform"],
    "graph": ["math", "core", "rhi"],
    "renderer": ["math", "core", "platform", "cert", "scene", "assets", "rhi", "graph"],
    "audio": ["math", "core", "platform"],
    "world": ["math", "core", "platform", "cert", "scene", "assets", "renderer", "audio"],
    "scripting": ["math", "core", "scene", "world"],
    "tools": ["math", "core", "platform", "cert", "scene", "assets", "rhi", "graph", "renderer",
              "audio", "world", "scripting"],
    "editor": ["math", "core", "platform", "cert", "scene", "assets", "rhi", "graph", "renderer",
               "audio", "world", "scripting", "tools"],
}
# System headers that belong to one backend or platform directory.
API_HEADERS = [
    (re.compile(r"(windows|d3d12|d3d12sdklayers|dxgi\w*|d3dcompiler)\.h$|^wrl/"), ("src/rhi/d3d12/", "src/platform/win32/")),
    (re.compile(r"^webgpu/"), ("src/rhi/webgpu/",)),
    (re.compile(r"^(vulkan/|vk_mem_alloc)"), ("src/rhi/vulkan/",)),
    (re.compile(r"^(Metal/|QuartzCore/)"), ("src/rhi/metal/", "src/platform/apple/")),
]
# Vendored headers (third_party/) and the files allowed to include them.
VENDORED = {"superstack.hpp": ("src/tools/",)}
# Headers CMake generates into the build tree, and the files that include them.
GENERATED = {"raw_d3d12_dxil.hpp": ("src/renderer/gpu/",), "raw_gpu_shaders.hpp": ("src/renderer/gpu/",)}
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)


def layer_of(rel):
    parts = rel.split("/")
    if parts[0] == "app":
        return "tools"
    if parts[0] in ("raw", "src") and len(parts) > 2:
        return parts[1]
    return None


def check_file(rel, text, exists):
    """Violations for one file. rel is the repo-relative path with / separators."""
    out = []
    layer = layer_of(rel)
    if rel.startswith("tests/"):
        return out
    if layer is None:
        if rel.startswith("raw/") and rel.count("/") == 1:
            return out          # a deprecated forwarder: one include, checked by its target's rules
        return [f"{rel}: not in a layer directory (raw/<layer>/, src/<layer>/ or app/)"]
    if layer not in LAYERS:
        return [f"{rel}: layer '{layer}' is not declared in scripts/check_layers.py"]
    allowed = set(LAYERS[layer]) | {layer}
    for kind, inc in INCLUDE.findall(text):
        if kind == "<":
            for pat, dirs in API_HEADERS:
                if pat.search(inc) and not rel.startswith(dirs):
                    out.append(f"{rel}: includes the API header <{inc}>, allowed only in {', '.join(dirs)}")
            continue
        m = re.fullmatch(r"raw/(\w+)/[\w/]+\.hpp", inc)
        if m:
            if m.group(1) not in allowed:
                out.append(f"{rel}: layer {layer} may not include {inc} (layer {m.group(1)})")
            continue
        if re.fullmatch(r"raw/\w+\.hpp", inc):
            out.append(f"{rel}: includes the deprecated path {inc}; use raw/<layer>/{inc[4:]}")
            continue
        name = inc.split("/")[-1]
        if inc in VENDORED:
            if not rel.startswith(VENDORED[inc]):
                out.append(f"{rel}: includes vendored {inc}, allowed only in {', '.join(VENDORED[inc])}")
            continue
        if inc in GENERATED:
            if not rel.startswith(GENERATED[inc]):
                out.append(f"{rel}: includes generated {inc}, allowed only in {', '.join(GENERATED[inc])}")
            continue
        if rel.startswith("raw/"):
            out.append(f"{rel}: a public header includes the private header {inc}")
            continue
        target = (Path(rel).parent / inc).as_posix()
        if not exists(target):
            out.append(f"{rel}: includes {inc}, which is not a file beside it")
        elif layer_of(target) != layer:
            out.append(f"{rel}: includes {target}, a private header of layer {layer_of(target)}")
        elif name and target.startswith("src/rhi/") and not target.startswith(str(Path(rel).parent.as_posix()) + "/"):
            out.append(f"{rel}: includes {target} from another backend")
    return out


def sources():
    for d in ("raw", "src", "app", "tests"):
        for p in sorted((ROOT / d).rglob("*")):
            if p.suffix in (".hpp", ".cpp", ".h"):
                yield p.relative_to(ROOT).as_posix(), p.read_text(encoding="utf-8")


def report():
    edges = {}
    for rel, text in sources():
        layer = layer_of(rel)
        if not layer or rel.startswith("tests/"):
            continue
        for kind, inc in INCLUDE.findall(text):
            m = re.fullmatch(r"raw/(\w+)/[\w/]+\.hpp", inc) if kind == '"' else None
            if m and m.group(1) != layer:
                edges.setdefault(layer, set()).add(m.group(1))
    for layer in LAYERS:
        if layer in edges:
            print(f"{layer} -> {', '.join(sorted(edges[layer]))}")


def selftest():
    """The checker must refuse each kind of violation it exists to catch."""
    files = {"src/core/a.cpp", "src/core/p.hpp", "src/renderer/r.hpp", "src/rhi/d3d12/d.hpp", "src/rhi/webgpu/w.cpp"}
    exists = files.__contains__
    refused = {
        "upward include": ("src/core/a.cpp", '#include "raw/renderer/render.hpp"\n'),
        "skipping a layer rule": ("src/rhi/d3d12/x.cpp", '#include "raw/scene/scene.hpp"\n'),
        "API header outside its backend": ("src/renderer/x.cpp", "#include <d3d12.h>\n"),
        "webgpu outside its backend": ("src/graph/x.cpp", "#include <webgpu/webgpu.h>\n"),
        "deprecated forwarder": ("src/scene/x.cpp", '#include "raw/vec.hpp"\n'),
        "private header of another layer": ("src/core/x.cpp", '#include "../renderer/r.hpp"\n'),
        "public header including a private one": ("raw/core/x.hpp", '#include "p.hpp"\n'),
        "vendored header outside its layer": ("src/core/x.cpp", '#include "superstack.hpp"\n'),
        "undeclared layer": ("src/physics/x.cpp", '#include "raw/core/arena.hpp"\n'),
        "another backend's private header": ("src/rhi/webgpu/w.cpp", '#include "../d3d12/d.hpp"\n'),
    }
    for what, (rel, text) in refused.items():
        if not check_file(rel, text, exists):
            print(f"check_layers selftest: accepted {what}", file=sys.stderr)
            return 1
    accepted = {
        "own layer and an allowed one": ("src/renderer/x.cpp", '#include "raw/renderer/render.hpp"\n#include "raw/graph/frame_graph.hpp"\n'),
        "private header beside it": ("src/core/a.cpp", '#include "p.hpp"\n'),
        "API header in its backend": ("src/rhi/d3d12/x.cpp", "#include <d3d12.h>\n#include <wrl/client.h>\n"),
        "standard headers": ("src/core/x.cpp", "#include <vector>\n#include <span>\n"),
    }
    for what, (rel, text) in accepted.items():
        if check_file(rel, text, exists):
            print(f"check_layers selftest: refused {what}: {check_file(rel, text, exists)}", file=sys.stderr)
            return 1
    print("check_layers: selftest passed")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if "--report" in argv:
        report()
        return 0
    exists = lambda rel: (ROOT / rel).is_file()
    bad, n = [], 0
    for rel, text in sources():
        n += 1
        bad += check_file(rel, text, exists)
    for line in bad:
        print(line)
    print(f"check_layers: {n} files, " + ("every include allowed" if not bad else f"{len(bad)} violation(s)"))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

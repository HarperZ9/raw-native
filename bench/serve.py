#!/usr/bin/env python3
"""Static server for the benchmark page with cross-origin isolation headers
(COOP same-origin, COEP require-corp), which the pthreads build needs for
SharedArrayBuffer. Usage: python bench/serve.py [port]   (serves the repo root)"""
import functools, http.server, sys
from pathlib import Path


class Isolated(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".mjs": "text/javascript", ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def write_sums(root):
    """Record the SHA-256 of each built wasm variant for the page to check."""
    import hashlib, json
    sums = {}
    for variant in ("wasm", "wasm-simd", "wasm-threads"):
        d = root / f"build-{variant}"
        if (d / "raw-native.wasm").exists():
            sums[variant] = {ext: hashlib.sha256((d / f"raw-native.{ext}").read_bytes()).hexdigest()
                             for ext in ("mjs", "wasm")}
    gpu = root / "build-wasm-gpu"
    if (gpu / "raw-native-gpu.wasm").exists():
        sums["wasm-gpu"] = {ext: hashlib.sha256((gpu / f"raw-native-gpu.{ext}").read_bytes()).hexdigest()
                            for ext in ("mjs", "wasm")}
    (root / "build-wasm-sums.json").write_text(json.dumps(sums, indent=1))


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    root = Path(__file__).resolve().parent.parent
    write_sums(root)
    handler = functools.partial(Isolated, directory=str(root))
    http.server.ThreadingHTTPServer(("127.0.0.1", port), handler).serve_forever()

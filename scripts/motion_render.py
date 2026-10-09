#!/usr/bin/env python3
"""Render a Motion scene to video, frame-exact, on the GPU through headless Chrome.

    python scripts/motion_render.py --root DIR --serve 8765          # serve DIR for the live player
    python scripts/motion_render.py --root DIR --scene PATH --out film.mp4 \\
        [--width 3840 --height 2160 --fps 30] [--from S --to S] \\
        [--audio narration.wav] [--score score.wav --score-db -20] [--codec hevc_nvenc]

DIR is served on localhost (a secure origin, so WebGPU is on). --page is the
capture page under DIR (default web/motion/capture.html) and --scene the scene
module, relative to that page. Frame i is drawn at exactly i / fps by
web/motion/capture.html, read back as RGBA rows and piped to ffmpeg; the
narration (and an optional score under it) is muxed afterwards. Prints and
writes <out>.stats.json with the frame times the page measured.

Needs Python Playwright with Chrome or Chromium that has WebGPU, and ffmpeg
(on PATH, or --ffmpeg, or the imageio-ffmpeg binary when it is installed).
"""

from __future__ import annotations

import argparse
import http.server
import json
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from urllib.parse import parse_qs, urlparse

TYPES = {".mjs": "text/javascript", ".js": "text/javascript", ".wgsl": "text/plain", ".json": "application/json",
         ".html": "text/html", ".wav": "audio/wav", ".css": "text/css", ".woff2": "font/woff2"}


def find_ffmpeg(arg: str | None) -> str:
    if arg:
        return arg
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        sys.exit("ffmpeg not found: pass --ffmpeg")


class Sink:
    """Receives raw frames (to the ffmpeg encoder) or encoded chunks (to a file), in order."""

    def __init__(self, cmd: list[str] | None, first: int, frame_bytes: int, stream: Path | None = None):
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE) if cmd else None
        self.file = open(stream, "wb") if stream else None
        self.seq = 0
        self.next = first
        self.frame_bytes = frame_bytes
        self.done = threading.Event()
        self.stats: dict = {}
        self.error = ""
        self.t0 = time.time()

    def frame(self, i: int, data: bytes) -> str:
        if i != self.next:
            return f"expected frame {self.next}, got {i}"
        if len(data) != self.frame_bytes:
            return f"frame {i}: {len(data)} bytes, expected {self.frame_bytes}"
        self.proc.stdin.write(data)
        self.next += 1
        if i % 150 == 0:
            print(f"  frame {i}  {time.time() - self.t0:7.1f} s", flush=True)
        return ""

    def chunk(self, seq: int, data: bytes) -> str:
        if seq != self.seq:
            return f"expected chunk {self.seq}, got {seq}"
        self.file.write(data)
        self.seq += 1
        return ""

    def close(self):
        if self.proc:
            self.proc.stdin.close()
            self.proc.wait()
        if self.file:
            self.file.close()


def handler(root: Path, sink: Sink):
    class H(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **k):
            super().__init__(*a, directory=str(root), **k)

        def guess_type(self, path):
            return TYPES.get(Path(str(path)).suffix, super().guess_type(path))

        def log_message(self, *a):
            pass

        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def do_POST(self):
            u = urlparse(self.path)
            n = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(n)
            if u.path in ("/frame", "/chunk"):
                qs = parse_qs(u.query)
                err = sink.frame(int(qs["i"][0]), body) if u.path == "/frame" else sink.chunk(int(qs["seq"][0]), body)
                self.send_response(400 if err else 204)
                self.end_headers()
                if err:
                    sink.error = err
                    sink.done.set()
            elif u.path == "/done":
                sink.stats = json.loads(body or b"{}")
                self.send_response(204)
                self.end_headers()
                sink.done.set()
            else:
                self.send_response(404)
                self.end_headers()
    return H


def encoder(ffmpeg: str, w: int, h: int, fps: int, codec: str, quality: int, out: Path) -> list[str]:
    cmd = [ffmpeg, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", f"{w}x{h}", "-r", str(fps), "-i", "-"]
    if codec.endswith("_nvenc"):
        cmd += ["-c:v", codec, "-preset", "p6", "-tune", "hq", "-rc", "vbr", "-cq", str(quality), "-b:v", "0", "-pix_fmt", "yuv420p"]
        if codec.startswith("hevc"):
            cmd += ["-tag:v", "hvc1"]
    else:
        cmd += ["-c:v", codec, "-preset", "slow", "-crf", str(quality), "-pix_fmt", "yuv420p"]
    return cmd + ["-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709", "-movflags", "+faststart", str(out)]


def mux(ffmpeg: str, video: Path, out: Path, audio: str | None, score: str | None, score_db: float, seconds: float) -> None:
    """Lay the narration (and the score under it) on the video, padded or cut to exactly its length."""
    cmd = [ffmpeg, "-y", "-loglevel", "error", "-i", str(video)]
    if not audio and not score:
        shutil.copyfile(video, out)
        return
    inputs = [a for a in (audio, score) if a]
    for a in inputs:
        cmd += ["-i", a]
    # apad with -shortest can stall ffmpeg 7 once the video stream ends; pad to the exact length instead.
    pad = f"apad=whole_dur={seconds:.6f},atrim=0:{seconds:.6f}"
    if audio and score:
        # A mono narration is centred in stereo so a stereo score keeps its width.
        cmd += ["-filter_complex", f"[1:a]aformat=channel_layouts=stereo[n];[2:a]aformat=channel_layouts=stereo,volume={score_db}dB[s];"
                f"[n][s]amix=inputs=2:duration=longest:normalize=0,{pad}[a]", "-map", "0:v", "-map", "[a]"]
    else:
        cmd += ["-filter_complex", f"[1:a]{pad}[a]", "-map", "0:v", "-map", "[a]"]
    cmd += ["-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-t", f"{seconds:.6f}", "-map_metadata", "-1", "-movflags", "+faststart", str(out)]
    subprocess.run(cmd, check=True)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True)
    ap.add_argument("--page", default="web/motion/capture.html")
    ap.add_argument("--scene")
    ap.add_argument("--shaders", default="")
    ap.add_argument("--out")
    ap.add_argument("--serve", type=int, metavar="PORT", help="only serve --root on localhost:PORT, for the live player")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--from", dest="t0", type=float, default=0.0)
    ap.add_argument("--to", dest="t1", type=float, default=None)
    ap.add_argument("--warm", type=int, default=0, help="frames drawn before --from so history layers match a full run")
    ap.add_argument("--audio")
    ap.add_argument("--score")
    ap.add_argument("--score-db", type=float, default=-20.0)
    ap.add_argument("--transport", choices=["webcodecs", "raw"], default="webcodecs",
                    help="webcodecs: the browser's hardware encoder; raw: RGBA frames to ffmpeg (slower)")
    ap.add_argument("--wc-codec", default="avc1.640034", help="WebCodecs codec string (avc1 uses a fixed quantizer)")
    ap.add_argument("--qp", type=int, default=16, help="WebCodecs H.264 quantizer")
    ap.add_argument("--codec", default="hevc_nvenc", help="ffmpeg encoder for --transport raw")
    ap.add_argument("--quality", type=int, default=19, help="cq for NVENC, crf for x264/x265 (--transport raw)")
    ap.add_argument("--ffmpeg")
    ap.add_argument("--channel", default="chrome")
    ap.add_argument("--adapter", choices=["gpu", "swiftshader"], default="gpu",
                    help="swiftshader: Chrome's CPU WebGPU adapter, for machines with no GPU (CI)")
    ap.add_argument("--timeout", type=float, default=7200)
    a = ap.parse_args(argv)

    root = Path(a.root).resolve()
    if a.serve:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.serve), handler(root, None))
        print(f"serving {root} on http://localhost:{a.serve}/", flush=True)
        srv.serve_forever()
        return 0
    if not a.scene or not a.out:
        ap.error("--scene and --out are required to render")
    from playwright.sync_api import sync_playwright

    out = Path(a.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    silent = out.with_suffix(".video" + out.suffix)
    ffmpeg = find_ffmpeg(a.ffmpeg)
    first = round(a.t0 * a.fps)
    stream = out.with_suffix(".stream")
    if a.transport == "raw":
        sink = Sink(encoder(ffmpeg, a.width, a.height, a.fps, a.codec, a.quality, silent), first, a.width * a.height * 4)
    else:
        sink = Sink(None, first, 0, stream)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler(root, sink))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    port = srv.server_address[1]
    q = (f"scene={a.scene}&w={a.width}&h={a.height}&fps={a.fps}&from={first}&warm={a.warm}"
         f"&transport={a.transport}&codec={a.wc_codec}&qp={a.qp}&readback={1 if a.adapter == 'swiftshader' else 0}")
    if a.t1 is not None:
        q += f"&to={round(a.t1 * a.fps)}"
    if a.shaders:
        q += f"&shaders={a.shaders}"
    url = f"http://localhost:{port}/{a.page}?{q}"
    print(f"render {url}", flush=True)
    t0 = time.time()
    try:
        with sync_playwright() as p:
            b = p.chromium.launch(channel=a.channel or None, headless=True,
                                  args=["--enable-unsafe-webgpu", "--enable-gpu", "--ignore-gpu-blocklist"]
                                  + (["--enable-unsafe-swiftshader", "--use-webgpu-adapter=swiftshader"] if a.adapter == "swiftshader" else []))
            page = b.new_page()
            page.on("console", lambda m: m.type in ("error", "warning") and print("  page:", m.text, flush=True))
            page.goto(url)
            if not sink.done.wait(a.timeout):
                sink.error = "timed out"
            b.close()
    finally:
        srv.shutdown()
        sink.close()
    if sink.error or sink.stats.get("error"):
        print("FAILED:", sink.error or sink.stats.get("error"), file=sys.stderr)
        return 1
    if a.transport == "webcodecs":
        fmt = "h264" if a.wc_codec.startswith("avc1") else "hevc" if a.wc_codec.startswith(("hvc1", "hev1")) else None
        if not fmt:
            sys.exit("only avc1 and hvc1 streams can be muxed")
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-fflags", "+genpts", "-r", str(a.fps), "-f", fmt, "-i", str(stream),
                        "-c:v", "copy", *(["-tag:v", "hvc1"] if fmt == "hevc" else []), "-movflags", "+faststart", str(silent)], check=True)
        stream.unlink()
    frames = int(sink.stats.get("frames") or 0)
    mux(ffmpeg, silent, out, a.audio, a.score, a.score_db, frames / a.fps)
    silent.unlink(missing_ok=True)
    stats = {**sink.stats, "wall_seconds": round(time.time() - t0, 1), "width": a.width, "height": a.height, "fps": a.fps,
             "transport": a.transport}
    Path(str(out) + ".stats.json").write_text(json.dumps(stats, indent=1), encoding="utf-8")
    print(json.dumps({k: stats[k] for k in ("frames", "frame_ms_median", "frame_ms_p95", "wall_seconds") if k in stats}))
    return 0


if __name__ == "__main__":
    sys.exit(main())

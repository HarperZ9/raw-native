#!/usr/bin/env python3
"""raw-native media: render a repo's media from its spec, at the commit it is run at.

    raw-native media render [SCENE ...] [--spec docs/media/media.json] --out DIR
        [--width 1920 --height 1080] [--adapter gpu|swiftshader] [--narration DIR] [--setup]
    raw-native media facts  [--spec ...] [--setup]          # facts and recordings only
    raw-native media attach --tag vX.Y.Z DIR [--repo owner/name]

For each scene, DIR/<scene>/ gets:
- the video (<scene>-<h>p.mp4) and a 1080p web copy when the master is larger;
- poster.jpg and <scene>.vtt;
- facts.json, and <scene>.cast.json for a walkthrough;
- an interactive page: index.html with engine/ and scene/ beside it;
- media.json, listing every file with its SHA-256.
--narration DIR takes <scene>/narration.wav and timing.json, as the site's narrate tool writes them.
See docs/architecture/adr/0012-media-engine.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENGINE = HERE.parents[1]                      # the raw-native checkout this tool belongs to
sys.path.insert(0, str(HERE))
import media_spec  # noqa: E402

ENGINE_FILES = ["web/raw-gpu.mjs", "web/frame-graph.mjs", "web/threads.mjs", "web/worlds.mjs", "web/camera.mjs",
                "src/renderer/gpu/shaders/threads.wgsl", "src/renderer/gpu/shaders/worlds.wgsl"]
ATLAS = ENGINE / "docs" / "motion" / "hanken-grotesk-500.atlas.json"


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def ffmpeg() -> str:
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


TEXT_SUFFIXES = {".mjs", ".js", ".wgsl", ".json", ".html", ".css", ".vtt", ".md", ".txt"}


def copy_lf(src: Path, dst: Path) -> None:
    """Copy, with text files in LF whatever the checkout uses, so a bundle (and its hashes)
    is the same from a Windows checkout and a Linux runner."""
    data = src.read_bytes()
    if src.suffix in TEXT_SUFFIXES:
        data = data.replace(bytes([13, 10]), bytes([10]))
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)


def bundle(spec: dict, spec_path: Path, scene: dict, out: Path, facts: dict, cast: dict | None, narration: Path | None) -> str:
    """Lay out out/: engine/, scene/, facts and the page. Returns the scene module path relative to the capture page."""
    if out.exists():
        shutil.rmtree(out)
    (out / "engine" / "motion").mkdir(parents=True)
    for f in ENGINE_FILES:
        copy_lf(ENGINE / f, out / "engine" / Path(f).name)
    for f in (ENGINE / "web" / "motion").iterdir():
        if f.is_file() and not f.name.endswith(".test.mjs"):
            copy_lf(f, out / "engine" / "motion" / f.name)
    src = spec_path.parent / spec.get("scenes_dir", "scenes")
    (out / "scene").mkdir(exist_ok=True)
    if src.is_dir():
        for f in src.rglob("*"):
            if f.is_file():
                copy_lf(f, out / "scene" / f.relative_to(src))
    copy_lf(ATLAS, out / "scene" / "atlas.json")
    (out / "facts.json").write_text(json.dumps(facts, indent=1), encoding="utf-8", newline="\n")
    timing = None
    if narration:
        shutil.copyfile(narration / "timing.json", out / "timing.json")
        timing = "../timing.json"
    if scene["kind"] == "walkthrough":
        (out / f"{scene['id']}.cast.json").write_text(json.dumps(cast, indent=1), encoding="utf-8", newline="\n")
        steps = [{k: v for k, v in s.items() if k != "run"} for s in scene["steps"]]
        (out / "scene" / f"{scene['id']}.json").write_text(json.dumps({"title": scene["title"], "subtitle": scene.get("subtitle", ""), "steps": steps}, indent=1), encoding="utf-8", newline="\n")
        mod = out / "scene" / f"{scene['id']}.scene.mjs"
        tm = f', timing: "{timing}"' if timing else ""
        mod.write_text(f'import {{ walkthroughScene }} from "@raw-native/motion/walkthrough.mjs";\n'
                       f'export default walkthroughScene({{ spec: "{scene["id"]}.json", cast: "../{scene["id"]}.cast.json", atlas: "atlas.json"{tm} }});\n',
                       encoding="utf-8", newline="\n")
        name = mod.name
    else:
        name = Path(scene["module"]).name
    page = (HERE / "page.html").read_text(encoding="utf-8")
    page = page.replace("{{TITLE}}", scene["title"]).replace("{{SCENE}}", f"scene/{name}").replace("{{ID}}", scene["id"])
    (out / "index.html").write_text(page, encoding="utf-8", newline="\n")
    return f"../../scene/{name}"


def write_script(out: Path, scene: dict, vtt: str) -> None:
    """The words to narrate, one segment per caption cue, in the explainer-film/1 shape the
    narration tool reads. A walkthrough's segment 0 is its title, so step k is segment k + 1."""
    cues = [b.split("\n", 1)[1].strip() for b in vtt.replace("\r", "").split("\n\n")[1:] if "-->" in b]
    title = [{"key": "title", "lines": [scene["title"].replace(":", ".") + "."]}] if scene["kind"] == "walkthrough" else []
    segs = title + [{"key": f"c{k}", "lines": [c.replace("\n", " ")]} for k, c in enumerate(cues)]
    doc = {"schema": "explainer-film/1", "slug": scene["id"], "title": scene["title"], "segments": segs}
    (out / f"{scene['id']}.script.json").write_text(json.dumps(doc, indent=1), encoding="utf-8", newline="\n")


def render(args) -> int:
    spec_path = Path(args.spec).resolve()
    spec = media_spec.load(spec_path)
    root = media_spec.repo_root(spec_path.parent)
    if args.setup:
        media_spec.setup(spec, root)
    scenes = [s for s in spec["scenes"] if not args.scenes or s["id"] in args.scenes]
    # Walkthroughs run first: they may be the build the facts need.
    casts = {s["id"]: media_spec.record(s, spec, root) for s in scenes if s["kind"] == "walkthrough"}
    facts = media_spec.facts(spec, root)
    if args.facts_only:
        print(json.dumps({k: (v["value"] if not isinstance(v["value"], list) else "[grid]") for k, v in facts["facts"].items()}, indent=1))
        return 0
    outroot = Path(args.out).resolve()
    ff = ffmpeg()
    for s in scenes:
        out = outroot / s["id"]
        cast = casts.get(s["id"])
        nar = Path(args.narration) / s["id"] if args.narration else None
        if nar and not (nar / "narration.wav").is_file():
            nar = None
        scene_rel = bundle(spec, spec_path, s, out, facts, cast, nar)
        h = args.height
        video = out / f"{s['id']}-{h}p.mp4"
        cmd = [sys.executable, str(ENGINE / "scripts" / "motion_render.py"), "--root", str(out), "--page", "engine/motion/capture.html",
               "--scene", scene_rel, "--shaders", "../", "--out", str(video), "--width", str(args.width), "--height", str(h),
               "--adapter", args.adapter]
        if nar:
            cmd += ["--audio", str(nar / "narration.wav")]
        if args.hash_frames:
            cmd += ["--hash-frames"]
        if args.max_seconds:
            cmd += ["--to", str(args.max_seconds)]
        print(f"render {s['id']} at {args.width}x{h} on {args.adapter}", flush=True)
        subprocess.run(cmd, check=True)
        stats = json.loads(Path(str(video) + ".stats.json").read_text(encoding="utf-8"))
        Path(str(video) + ".stats.json").unlink()
        frames_file = Path(str(video) + ".frames.sha256")
        if frames_file.is_file():
            frames_file.replace(out / "frames.sha256")
        mixwav = video.with_suffix(".mix.wav")
        if mixwav.is_file():
            mixwav.unlink()     # its hash and loudness are in the manifest; the MP4 carries the audio
        if h > 1080:
            web = out / f"{s['id']}-1080p.mp4"
            subprocess.run([ff, "-y", "-loglevel", "error", "-i", str(video), "-vf", "scale=1920:1080:flags=lanczos", "-c:v", "libx264",
                            "-preset", "slow", "-crf", "21", "-pix_fmt", "yuv420p", "-c:a", "copy", "-movflags", "+faststart", str(web)], check=True)
        else:
            web = video
        subprocess.run([ff, "-y", "-loglevel", "error", "-ss", f"{min(stats['duration'], stats['frames'] / 30) * float(s.get('poster_at', 0.4)):.3f}", "-i", str(web),
                        "-frames:v", "1", "-q:v", "3", str(out / "poster.jpg")], check=True)
        if stats.get("vtt"):
            (out / f"{s['id']}.vtt").write_text(stats["vtt"], encoding="utf-8", newline="\n")
            write_script(out, s, stats["vtt"])
        files = sorted(p for p in out.rglob("*") if p.is_file() and p.name != "media.json")
        manifest = {
            "schema": "raw-native.media-render/1", "scene": s["id"], "title": s["title"], "kind": s["kind"],
            "commit": facts["commit"], "narrated": bool(nar), "captions_burned": bool(s.get("captions_burned")),
            "width": args.width, "height": h, "fps": stats.get("fps", 30),
            "duration": round(stats["duration"], 3), "frames": stats["frames"], "adapter": stats.get("adapter"), "adapter_kind": args.adapter,
            "frame_ms_median": round(stats["frame_ms_median"], 2), "frame_ms_p95": round(stats["frame_ms_p95"], 2), "wall_seconds": stats["wall_seconds"],
            "chapters": stats.get("chapters", []),
            "audio": stats.get("audio") or None,
            "frame_chain_sha256": stats.get("frame_chain_sha256"),
            "encoder_probe": stats.get("encoder_probe"), "user_agent": stats.get("user_agent"),
            "gpu_ms": stats.get("gpu_ms"),
            "files": {str(p.relative_to(out)).replace("\\", "/"): sha(p) for p in files},
        }
        (out / "media.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8", newline="\n")
        print(f"{s['id']}: {stats['frames']} frames, median {stats['frame_ms_median']:.1f} ms, {stats['wall_seconds']} s", flush=True)
    return 0


def engine_commit() -> str:
    r = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ENGINE, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def repage(args) -> int:
    """Refresh the interactive page and engine of rendered bundles, leaving the video, facts and
    recordings as rendered, and rehash the manifest. For fixes to the live page after a release."""
    root = Path(args.dir).resolve()
    dirs = [root] if (root / "media.json").is_file() else sorted(p for p in root.iterdir() if (p / "media.json").is_file())
    spec = {s["id"]: s for s in media_spec.load(Path(args.spec).resolve())["scenes"]} if args.spec else {}
    for d in dirs:
        m = json.loads((d / "media.json").read_text(encoding="utf-8"))
        sid = m["scene"]
        for f in ENGINE_FILES:
            copy_lf(ENGINE / f, d / "engine" / Path(f).name)
        for f in (ENGINE / "web" / "motion").iterdir():
            if f.is_file() and not f.name.endswith(".test.mjs"):
                copy_lf(f, d / "engine" / "motion" / f.name)
        module = next((p.name for p in (d / "scene").glob(f"{sid}.scene.mjs")), None) or Path(spec[sid]["module"]).name
        page = (HERE / "page.html").read_text(encoding="utf-8")
        page = page.replace("{{TITLE}}", m["title"]).replace("{{SCENE}}", f"scene/{module}").replace("{{ID}}", sid)
        (d / "index.html").write_text(page, encoding="utf-8", newline="\n")
        if sid in spec:
            m["captions_burned"] = bool(spec[sid].get("captions_burned"))
        keep = {k for k in m["files"] if not (d / k).is_file()}       # masters kept only on the release
        files = sorted(p for p in d.rglob("*") if p.is_file() and p.name != "media.json")
        m["files"] = {**{k: m["files"][k] for k in keep}, **{str(p.relative_to(d)).replace("\\", "/"): sha(p) for p in files}}
        m["repaged"] = {"engine_commit": engine_commit(), "note": "page and engine refreshed; video, facts and recordings as rendered"}
        (d / "media.json").write_text(json.dumps(m, indent=1), encoding="utf-8", newline="\n")
        print(f"repaged {sid}")
    return 0


def attach(args) -> int:
    """Upload rendered scenes to a GitHub release: each video and poster, and the whole folder as a zip."""
    src = Path(args.dir).resolve()
    assets = []
    for d in sorted(p for p in src.iterdir() if p.is_dir()):
        sid = d.name
        for v in d.glob(f"{sid}-*p.mp4"):
            assets.append(v)
        if (d / "poster.jpg").is_file():
            p = src / f"{sid}-poster.jpg"
            shutil.copyfile(d / "poster.jpg", p)
            assets.append(p)
        z = shutil.make_archive(str(src / f"{sid}-media-{args.tag}"), "zip", d)
        assets.append(Path(z))
    repo = ["-R", args.repo] if args.repo else []
    subprocess.run(["gh", "release", "upload", args.tag, *map(str, assets), "--clobber", *repo], check=True)
    print("attached:", ", ".join(a.name for a in assets))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="raw-native media", description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("render", "facts"):
        p = sub.add_parser(name)
        p.add_argument("scenes", nargs="*")
        p.add_argument("--spec", default="docs/media/media.json")
        p.add_argument("--setup", action="store_true", help="run the spec's setup commands first (a build)")
        if name == "render":
            p.add_argument("--out", required=True)
            p.add_argument("--width", type=int, default=1920)
            p.add_argument("--height", type=int, default=1080)
            p.add_argument("--adapter", choices=["gpu", "swiftshader"], default="gpu")
            p.add_argument("--narration")
            p.add_argument("--max-seconds", type=float, default=None, help="render only the first N seconds (a smoke test)")
            p.add_argument("--hash-frames", action="store_true", help="read back and hash every frame (frames.sha256 in the bundle)")
        p.set_defaults(fn=render, facts_only=(name == "facts"))
    p = sub.add_parser("attach")
    p.add_argument("dir")
    p.add_argument("--tag", required=True)
    p.add_argument("--repo")
    p.set_defaults(fn=attach)
    p = sub.add_parser("repage", help="refresh the interactive page and engine of rendered bundles")
    p.add_argument("dir")
    p.add_argument("--spec")
    p.set_defaults(fn=repage)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())

"""python tools/media/test_media.py: the media spec's pure parts (facts, images, path scrubbing)."""

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import media_spec  # noqa: E402


class Facts(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        (self.dir / "c.json").write_text(json.dumps({"exact": {"rmse": 0.129}, "list": [{"v": 7}]}), encoding="utf-8")
        (self.dir / "CMakeLists.txt").write_text("project(raw_native VERSION 0.6.0 LANGUAGES CXX)\n", encoding="utf-8")
        w = h = 4
        (self.dir / "a.pgm").write_bytes(b"P5\n4 4\n255\n" + bytes([0, 255] * 8))
        rows = [[float(y) for _ in range(w)] for y in range(h)]           # PFM rows run bottom to top
        (self.dir / "b.pfm").write_bytes(b"Pf\n4 4\n-1.0\n" + b"".join(struct.pack("<4f", *r) for r in rows))

    def spec(self, facts, **kw):
        return {"schema": "raw-native.media/1", "facts": facts, **kw}

    def test_kinds(self):
        f = media_spec.facts(self.spec({
            "rmse": {"json": "c.json", "pointer": "/exact/rmse"},
            "v": {"json": "c.json", "pointer": "/list/0/v"},
            "ver": {"text": "CMakeLists.txt", "regex": "VERSION ([0-9.]+)"},
            "echo": {"cmd": [sys.executable, "-c", "print('tests: 36')"], "regex": "tests: (\\d+)"},
            "img": {"image": "a.pgm", "size": 2},
        }), self.dir)["facts"]
        self.assertEqual(f["rmse"]["value"], 0.129)
        self.assertEqual(f["v"]["value"], 7)
        self.assertEqual(f["ver"]["value"], "0.6.0")
        self.assertEqual(f["echo"]["value"], "36")
        self.assertEqual(f["img"]["value"], [[0.5, 0.5], [0.5, 0.5]])

    def test_exit_codes_cwd_and_stdin(self):
        (self.dir / "sub").mkdir()
        (self.dir / "sub" / "in.json").write_text('{"v": 5}', encoding="utf-8")
        f = media_spec.facts(self.spec({
            "code": {"cmd": [sys.executable, "-c", "import sys; sys.exit(3)"], "exit": True},
            "here": {"cmd": [sys.executable, "-c", "import os; print(os.path.basename(os.getcwd()))"], "cwd": "sub"},
            "piped": {"cmd": [sys.executable, "-c", "import json,sys; print(json.load(sys.stdin)['v'] * 2)"], "stdin": "sub/in.json"},
        }), self.dir)["facts"]
        self.assertEqual(f["code"]["value"], 3)
        self.assertEqual(f["here"]["value"], "sub")
        self.assertEqual(f["piped"]["value"], "10")

    def test_pfm_rows_are_flipped_to_top_down(self):
        g = media_spec._image(self.dir / "b.pfm", 4)
        self.assertEqual([r[0] for r in g], [3.0, 2.0, 1.0, 0.0])

    def test_a_missing_fact_stops_the_render(self):
        with self.assertRaises(SystemExit):
            media_spec.facts(self.spec({"x": {"json": "c.json", "pointer": "/nope"}}), self.dir)

    def test_vars_take_the_first_path_that_exists(self):
        (self.dir / "build").mkdir()
        (self.dir / "build" / "tool").write_text("", encoding="utf-8")
        v = media_spec.resolve_vars({"vars": {"cli": ["build/Release/tool.exe", "build/tool"]}}, self.dir)
        self.assertEqual(v["cli"], "build/tool")
        self.assertEqual(media_spec.expand(["{cli}", "--out", "{nope}"], v), ["build/tool", "--out", "{nope}"])
        self.assertEqual(media_spec.local_exe("cmake", self.dir), "cmake")

    def test_scrub_removes_the_checkout_and_home(self):
        root = Path("D:/work/repo")
        self.assertEqual(media_spec.scrub("written to: D:/work/repo/build", root), "written to: build")
        if sys.platform == "win32":
            self.assertEqual(media_spec.scrub("D:\\work\\repo\\build\\x.exe", root), "build\\x.exe")
        self.assertEqual(media_spec.scrub(f"{Path.home().as_posix()}/cache", root), "~/cache")


if __name__ == "__main__":
    unittest.main()

"""Tests for the media package cutter (tools/media/package.py, package_text.py)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image  # noqa: E402

import package  # noqa: E402
import package_text as T  # noqa: E402


def test_wrap_keeps_42_characters_and_no_orphans():
    text = "Rest the pointer on a reply, and a window opens beside it: where the platform places the account, how old it is."
    lines = T.wrap(text)
    assert all(len(l) <= 42 for l in lines)
    assert all(len(l.split()) >= 3 for l in lines)
    assert " ".join(lines) == text


def test_cues_split_long_sentences_and_keep_timing():
    rows = [{"start": 1.0, "end": 9.0, "text": "word " * 40}]
    cs = T.cues(rows, offset=0.5)
    assert all(len(c["lines"]) <= 2 for c in cs)
    assert abs(cs[0]["start"] - 1.5) < 1e-9 and abs(cs[-1]["end"] - 9.5) < 1e-6
    assert all(a["end"] <= b["start"] + 1e-9 for a, b in zip(cs, cs[1:]))


def test_view_box_keeps_the_output_aspect_inside_the_source():
    img = Image.new("RGB", (3840, 2160))
    for rect, aspect in (((0, 0, 3840, 2160), 9 / 16), ((3000, 1800, 800, 300), 1.0), ((100, 0, 3800, 3000), 16 / 9)):
        x0, y0, x1, y1 = package.view_box(img, rect, 16 / 9, aspect, 0.8)
        assert 0 <= x0 < x1 <= 3840 and 0 <= y0 < y1 <= 2160
        assert abs((x1 - x0) / (y1 - y0) - aspect) < 0.01


def test_vertical_crop_follows_the_focus():
    img = Image.new("RGB", (3840, 2160))
    left = package.view_box(img, None, 16 / 9, 9 / 16, 0.1)
    right = package.view_box(img, None, 16 / 9, 9 / 16, 0.9)
    assert left[0] == 0 and right[2] == 3840

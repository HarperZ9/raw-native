- **Media package cutter** (`tools/media/package.py`, `tools/media/package_text.py`). One JSON
  timeline cuts a 4K master and narrower social cuts (9:16, 1:1) from engine image sequences,
  screen-capture stills with eased crops, and title and terminal cards, with narration under the
  picture and captions burned in at two lines of 42 characters with no short orphans. Each cut is
  composed at its own size. A research tool; it ships in no product. 4 tests in
  `tools/media/test_package.py`.

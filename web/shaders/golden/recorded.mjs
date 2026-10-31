// CPU fingerprints of the film, CRT and lens presets. Film re-recorded 2026-10-31 on purpose: the named presets took the
// digitised curves and the print's dyes were re-chosen for them (before and after in docs/shaders/CONSTANTS.md). CRT and
// lens are unchanged since 726a98a. Re-record only for a deliberate look change: node web/shaders/golden/presets.mjs --write prints the block.
export const RECORDED = {
 "film": {
  "500t-print": "b2b90237e60b845a",
  "500t-no-remjet": "cd81d5c0a756aab6",
  "250d-print": "1c981ad4a5a5a22e",
  "bleach-bypass": "c1722687a7ebce4b"
 },
 "crt": {
  "pvm-20": "76a10e3b2a649e60",
  "pvm-20-soft": "0fb14840e57e96bc",
  "trinitron-tv": "2fcf3995e99dbe45",
  "slot-tv": "ee84470e0f1cddb7",
  "vga-14": "3dc57b8bd082584c",
  "bw-tv": "6c1469f9ba5c432d",
  "radar-p7": "ff9744bd6f78a129",
  "scope-p31": "beb093c2ecdb4f39"
 },
 "lens": {
  "default": "446bb15c64b4d3ea"
 }
};

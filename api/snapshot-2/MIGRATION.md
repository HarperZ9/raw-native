# From snapshot 1 to snapshot 2

Date: 2026-10-12. Cause: the anisotropic split-albedo table of `raw::pbr::Tables` became denser and is
now generated once and embedded, instead of being computed by every `Tables()` (method note 4 of
2026-10-12 in `evidence/m3-materials2-bounds.json`).

## What changed

| Declaration | Snapshot 1 | Snapshot 2 |
|---|---|---|
| `Tables::kAnPhi` | 8 | 16 |
| `Tables::kAnRough` | 24 | 48 |
| `Tables::kAnSamples` | `1 << 10` | `1 << 14` |
| `raw::kApiSnapshot` | 1 | 2 |

The grid constants still describe the `A4` and `B4` table, so code that sizes a buffer from them, or
indexes `gridA4()` and `gridB4()` with them, keeps working once it is rebuilt. Values read through
`A4()`, `B4()`, `Aavg2()` and `Bavg2()` change by up to about 2e-3 where the old grid was coarse.

## What was added

- `Tables::kAnAll`: entries per anisotropic table.
- `Tables::anisoEntry()`: one entry computed by sampling, as the generator computes it.
- `Tables::anisoBytes()` and `Tables::anisoByteCount()`: the embedded table.
- `Tables::kHatAnMu`, `kHatAnPhi`, `kHatAnRough`, `kHatAnK`, `kHatAnSamples` and `kHatAnAll`: the grid
  of the coat compensation's anisotropic hat tables (`HA4`, `HB4`, `gridHA4()`, `gridHB4()`,
  `gridHm4()`). These tables used the `kAn*` grid in snapshot 1 and keep its old values.

## How to update

Code that indexed `gridHA4()`, `gridHB4()` or `gridHm4()` with the `kAn*` constants must switch to the
`kHatAn*` constants. Nothing else needs a change.

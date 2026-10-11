#pragma once
// The numbered snapshot of the renderer and scene APIs (ADR 0015, roadmap M3 criterion 5).
// api/snapshot-N/ holds its manifest and declaration dump; scripts/api_dump.py --check fails a
// change that removes or alters a declaration the snapshot lists. Such a break moves to the
// next number here, with a migration note in api/snapshot-N/MIGRATION.md.
namespace raw {
inline constexpr int kApiSnapshot = 1;
}

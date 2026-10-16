#pragma once
// raw_native_cli style-parity: the style passes ported into the C++ core (ADR 0019) against the
// JavaScript references and, on a GPU build, on this build's backend
// (raw/renderer/style_parity.hpp; bounds in evidence/cpp-port-style-bounds.json).
//   style-parity [--fixtures DIR] [--cpu-only] [--out FILE]
// DIR (default style-fixtures) holds what tools/port/export_style_fixtures.mjs writes. JSON on
// stdout (and in FILE). Exit 0 pass, 1 fail, 2 bad arguments or unreadable fixtures, 4 when a
// GPU link was wanted and no GPU backend or adapter is available (the CPU link still runs and
// is reported).
namespace raw {
int styleParityCommand(int argc, char** argv);
}

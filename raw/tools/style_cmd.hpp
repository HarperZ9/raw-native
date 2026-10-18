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
// raw_native_cli style-4k [--width W] [--height H] [--out FILE]: style passes over a W x H frame
// (default 3840 x 2160) through the dispatch every style pass uses. Exit 0 pass, 1 fail, 2 bad
// arguments, 4 no GPU backend or adapter.
int styleDispatchCommand(int argc, char** argv);
}

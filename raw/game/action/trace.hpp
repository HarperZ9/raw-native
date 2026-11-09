// raw/game/action/trace: replay of action-timeline traces (docs/action-timeline.md). A trace is
// JSON lines in the story trace's line form (docs/story-trace.md): inputs (abegin, ain, ahit,
// aend) and outputs (aev, ast). The replayer reads the inputs, runs the Timeline, and writes the
// whole trace again; a byte-identical result is parity with the engine that recorded it.
// Layer: game.
#pragma once
#include "raw/game/action/timeline.hpp"
#include <functional>
#include <string>

namespace raw::game::action {

// tables.json beside the traces: {"pool": {hash: defined move}, "tables": {name: {id: hash}}}
struct TraceTables { Json pool, tables; };
TraceTables load_trace_tables(const std::string& text);
MoveTable table_of(const TraceTables& t, const std::string& name);

struct TraceResult { std::string trace; std::string error; };
// tweak: optional change to the configuration read from the trace (the planted checks use it).
TraceResult replay_trace(const std::string& recorded, const TraceTables& tables,
                         const std::function<void(Config&)>& tweak = {});
// 0 when equal, else the 1-based line of the first difference.
size_t first_line_difference(const std::string& a, const std::string& b);

}  // namespace raw::game::action

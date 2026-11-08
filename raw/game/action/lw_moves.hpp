// raw/game/action/lw_moves: the lw-moves v1 file (Low Water's combat/data/moves.json) and the tool
// strike generator, ported from combat/data/tools.mjs buildToolMoves. The file holds the fixed
// tables and the tool settings; strikes are generated from the settings. The generator carries
// constants the file does not (wind-up tell minimum 18 frames, light hit-stop 4 and 6 frames,
// shake 0.15 and 0.3); they are fields of ToolGen so a game can set them. Layer: game.
#pragma once
#include "raw/game/action/moves.hpp"

namespace raw::game::action {

struct LwMoves {
    Json common, body, body_moveset, tool_classes, tools, foe_specials, boss_specials, arena_specials;
};
LwMoves load_lw_moves(const std::string& text);   // throws on a wrong format, version or fps

struct ToolGen {
    std::string prefix;
    bool slow = false;      // the tool is heavier than the wielder's grip: 1.3x slower, double cost
    double windup = 1;      // opponents' wind-ups (Low Water: 1.6) run longer and carry a tell
    int tell_min = 18;
    double light_hitstop[2] = {4, 6};
    double light_shake[2] = {0.15, 0.3};
};
// A tool profile: the class settings, then the tool record over them, with "id" and "cls".
Json tool_profile(const LwMoves& lw, const std::string& tool_id);
Json build_tool_moves(const Json& profile, const ToolGen& g);

// Every move after define_moves, in the shape of Low Water's tests/moves-expanded.golden.json:
// {"base": the tables plus every tool class at Lamb's wind-up, "tools": {id: {foe, lamb, slow}}}.
Json expand_all(const LwMoves& lw, double foe_windup = 1.6);
// One line a move, sorted (tools/action/dump_frames.mjs writes the same from the JS engine):
// <set> <id> startup= active= recovery= free= cancel=<action>:<from>-<to>,...
std::vector<std::string> frame_lines(const Json& expanded);
std::string frame_line(const std::string& set, const Json& defined_move);

}  // namespace raw::game::action

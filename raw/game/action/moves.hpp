// raw/game/action/moves: moves as data for a fixed 60 Hz action timeline, independent of any one
// game. A move is counted in integer simulation frames (frame 0 is its first): startup, active,
// recovery, and typed window sections over those frames. Sections:
//
//   Cancel        a buffered action whose class bit is in `mask` may cut the move short
//   Invulnerable  hits pass through (dodge frames)
//   Parry         a parryable hit from the front is turned
//   Armor         poise damage is reduced by `value` (the immunity tier: 1, 2, ... 99)
//   Charge        holding the charge button at frame `from` charges up to `max` frames; released
//                 after `min` it becomes move `into`
//   TapRelease    letting go of the charge button on frames [from, to] turns the move into `into`
//   Buffer        a press made on these frames is held to the move until it lets it out or ends
//                 ("buffer early, cancel late"); a press made before them expires after the buffer
//   Tell          the telegraph a renderer shows (presentation only)
//   Hit           a hit volume is live (geometry stays with the game)
//
// Moves come in as JSON records in the lw-moves v1 schema (Low Water's combat/data/moves.json).
// define_moves fills the schema's defaults exactly as the JS defineMoves does; compile turns the
// defined records into typed Moves the timeline steps. Layer: game.
#pragma once
#include "raw/story/story.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace raw::game::action {

using Json = raw::story::Json;
inline constexpr int FPS = 60;
inline constexpr double DT = 1.0 / FPS;

enum class Sec : uint8_t { Cancel, Invulnerable, Parry, Armor, Charge, TapRelease, Buffer, Tell, Hit };
const char* sec_name(Sec s);

struct Section {
    Sec kind = Sec::Cancel;
    int from = 0, to = -1;     // inclusive frames
    uint64_t mask = 0;         // Cancel: the action classes it lets out
    double value = 0;          // Armor: the tier; Charge: max frames
    int min = 0;               // Charge: frames held before release counts
    int into = -1;             // Charge, TapRelease: the move it becomes
};

// Action names ("light", "roll", ...) as class bits. "any" in a cancel table means every class.
struct ActionClasses {
    std::vector<std::string> names;
    int bit(const std::string& name);              // interned; at most 64 classes
    int find(const std::string& name) const;       // -1 when unknown
    static uint64_t all() { return ~uint64_t(0); }
};

struct Move {
    std::string id, kind, anim, tell_kind;
    int startup = 0, active = 0, recovery = 0, total = 0;
    double stamina = 0, dmg = 0, poise = 0, hitstop = 0, composure = 0;
    bool parryable = true, blockable = true, knockdown = false, pierce = false, item = false;
    int then = -1;                                   // the move that follows on the last frame
    std::vector<std::pair<int, int>> next;           // (action class, move): a chain
    std::vector<Section> sections;
    const Section* find(Sec k) const;
    bool in(Sec k, int f) const;                     // f inside any section of kind k
    double armor_at(int f) const;
    bool cancels(int f, int action_bit) const;
    int free_frame() const { return total; }         // the first frame after the move
};

struct MoveTable {
    std::vector<Move> moves;
    std::map<std::string, int> index;
    ActionClasses classes;
    int find(const std::string& id) const { auto i = index.find(id); return i == index.end() ? -1 : i->second; }
    const Move& at(int i) const { return moves[(size_t)i]; }
};

// lw-moves v1: fill the defaults of every record (JS defineMoves); throws std::runtime_error on a
// record the JS check() refuses or a reference to an unknown move.
Json define_moves(const Json& table);
// Typed moves from defined records.
MoveTable compile(const Json& defined);
// Recursively sorted object keys (the order JSON.stringify of the JS golden writes).
Json sort_keys(const Json& j);
// Object helpers: put replaces a key or appends it.
void put(Json& o, const std::string& key, Json v);
Json num(double v);
Json pair(double a, double b);

}  // namespace raw::game::action

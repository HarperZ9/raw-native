#pragma once
// Look keys, the C++ core's port of web/look/keys.mjs (ADR 0019): look terms keyed to
// time-of-day slots. Eight slots by default and six accepted (author, 2026-10-15); fewer than six
// is refused. Slots sit on a 24-hour circle, so the last slot blends into the first.
// Interpolation is a periodic monotone cubic Hermite spline (Fritsch and Carlson, 1980): the
// curve passes through every key exactly and never leaves the range of the two keys beside it.
// Look keys run on the CPU and reach a pass as uniform values. They live in the renderer layer
// until the environment-state stream (ADR 0017) creates the world layer.
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace raw::style {

struct Slot { std::string name; double hour{0}; };
inline constexpr int kMinSlots = 6;
const std::vector<Slot>& slots8();
const std::vector<Slot>& slots6();
// Throws std::invalid_argument for fewer than kMinSlots slots or hours that do not increase
// inside [0, 24).
const std::vector<Slot>& checkSlots(const std::vector<Slot>& s);

struct Located { int i{0}, j{0}; double t{0}, span{0}; };
Located locate(const std::vector<Slot>& slots, double hour);

// One term: a value per slot, each with `dim` components (dim 1 for a scalar), and an optional range.
struct TermSpec {
    std::vector<std::vector<double>> values;   // values[slot][component]
    std::optional<double> min, max;
};

class LookKeys {
public:
    // Throws std::invalid_argument for a bad slot list, a wrong value count or a bad value.
    LookKeys(std::vector<Slot> slots, const std::map<std::string, TermSpec>& terms);
    const std::vector<Slot>& slots() const { return slots_; }
    std::vector<std::string> names() const;
    // A term at an hour, clamped to its range. Throws std::invalid_argument for an unknown term.
    std::vector<double> term(const std::string& name, double hour) const;
private:
    struct Series { std::vector<std::vector<double>> comps, tans; std::optional<double> min, max; };
    std::vector<Slot> slots_;
    std::map<std::string, Series> series_;
};

}  // namespace raw::style

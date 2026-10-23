#pragma once
// P23's focus director (evidence/tiltshift-bounds.json, design.director): where the tilt-shift
// lens focuses, driven by story intent, with an observer's attention (author, 2026-10-22: "Like a
// living world being observed"). Walk follows the player, talk the speaker, examine the prop, a
// fight stops the lens down until every fighter is sharp, a dream tilts the plane wrong. In walk the
// attention wanders, after an idle time, to a living point and back, and world events pull it by
// salience; talk and fight lock it. Racks are critically damped springs (analytic steps, no
// overshoot) on the focus distance in dioptres, the plane's tilt and the f-number's logarithm. Under
// reduced motion every change is a cut, and there is no drift, event pull or jitter. Engine-side and
// game-agnostic: a game passes points and intents, the director returns a lens.
#include "raw/renderer/art_tiltshift.hpp"
#include <string>
#include <vector>
namespace raw::focus {

using art::D3;
enum class Intent { Walk, Talk, Examine, Fight, Dream };
enum class Character { Naturalist, Accuser, Child };
struct Living { D3 p; std::string id; double salience{0.5}; };
struct WorldEvent { D3 p; std::string kind; double salience{0.8}; };
struct Inputs {
    Intent intent{Intent::Walk};
    D3 player{0, 0, 0}, speaker{0, 0, 0}, prop{0, 0, 0};
    std::vector<D3> fighters;          // points that must be sharp in a fight (heads and bodies)
    std::vector<Living> living;        // points of interest the attention may visit
    std::vector<WorldEvent> events;    // events that happen this step
    bool playerMoving{false}, reducedMotion{false};
};
struct CharacterParams {
    double omega{8}, idle{6}, dwell{3.5}, eventHold{2.5}, eventMin{0.5}, jitterD{0}, jitterHz{0};
    bool scan{false};   // the accuser: visit every living point in order before returning
};
CharacterParams characterParams(Character c);

struct State {
    double dioptre{0}, rack{0}, tiltDeg{90};   // in-focus dioptres (with jitter), the rack spring alone, tilt
    D3 focus{0, 0, 0};                         // the plane passes through here
    std::string target;                        // "player", "speaker", "prop", "fight", a living id, "event:<kind>"
    D3 targetPoint{0, 0, 0};
    double fNumber{2.8};
    bool racking{false};
};

class Director {
public:
    // base: the look's lens (K, f-number, tilt, axis); eye and view as the camera's.
    Director(Character c, const art::Lens& base, const art::View& view);
    const State& step(const Inputs& in, double dt);
    const State& state() const { return s_; }
    art::Lens lens() const;              // the lens for the current state
    art::LensGeom geom() const;
    // Controls (evidence/tiltshift-bounds.json): lockOff lets drift and events act in every intent;
    // damping below 1 makes the springs underdamped.
    bool lockOff{false};
    double damping{1.0};
    double jitterNow{0};                 // the dioptre offset jitter added this step (d5)
private:
    struct Spring { double x{0}, v{0}, target{0}; void retarget(double t, double w); void advance(double dt, double w, double zeta); };
    CharacterParams cp_;
    art::Lens base_;
    art::View view_;
    State s_;
    Spring sD_, sT_, sN_;
    double time_{0}, idleT_{0}, dwellT_{0}, eventT_{0};
    int mode_{0};                        // 0 on the intent's subject, 1 drifting, 2 on an event
    std::size_t next_{0};
    D3 evPoint_{0, 0, 0};
    std::string evKind_;
    bool first_{true};
    Intent last_{Intent::Walk};
    double axialOf(const D3& p) const;
    void aim(const std::string& who, const D3& p);
};

}  // namespace raw::focus

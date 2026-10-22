#pragma once
// The conversation room: a reply thread laid out as a diorama on a board, seen through P23's
// tilt-shift lens (Daylight Commons' room map, mission-control/2026-10-22-media). One figure a
// reply account, grouped by behaviour and never by identity: the poster on a dais, ordinary
// repliers, disclosed paid voices (a drift-coloured disc at the feet), undisclosed look-alikes (an
// ink disc), and a burst crowd that arrives together. Every figure is synthetic; positions come
// from a fixed generator, so a frame is the same on every run.
#include "raw/renderer/art_scene.hpp"
#include <vector>
namespace raw::art {

enum class RoomGroup { Poster, Ordinary, Paid, Lookalike, Burst };

struct RoomFigure {
    D3 foot{0, 0, 0};
    D3 albedo{0.5, 0.5, 0.5};
    RoomGroup group{RoomGroup::Ordinary};
    double arrive{-1};   // seconds; below zero: present from the start
    int obj{0};
};

// The room at time t: which figures stand, where (a burst figure drops in over kRoomDrop seconds
// from its arrival), and how strongly the markers show (0 hidden, 1 full).
struct RoomState {
    double t{0};
    double paidMark{0}, lookalikeMark{0};
    double burstAt{1e9};   // the burst's first arrival, seconds
};
inline constexpr double kRoomDrop = 0.45;

const std::vector<RoomFigure>& roomFigures();
// The figure's foot at time t, or false when it has not arrived yet.
bool roomFootAt(const RoomFigure& f, const RoomState& s, D3& foot);
// The scene reads this state while tracing; set it before renderFrame(..., SceneKind::Room).
void setRoomState(const RoomState& s);
const RoomState& roomState();
Hit traceRoom(const D3& o, const D3& d);   // art_scene_room.cpp

// Group centres, for focus targets and the compositor's labels.
D3 roomCentre(RoomGroup g);
Lighting roomLight();

}  // namespace raw::art

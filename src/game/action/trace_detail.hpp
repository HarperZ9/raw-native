// Private to src/game/action: the record readers and the state line shared by the two trace replayers
// (trace.cpp, contact_trace.cpp).
#pragma once
#include "raw/game/action/timeline.hpp"

namespace raw::game::action::trace_detail {
double n_of(const Json& o, const char* k);
bool b_of(const Json& o, const char* k);
Config config_of(const Json& c);
ActorDef actor_of(const Json& a);
Json state_of(const Timeline& tl, const Actor& F);
}  // namespace raw::game::action::trace_detail

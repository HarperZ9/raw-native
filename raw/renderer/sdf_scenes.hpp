#pragma once
// Owned SDF scenes for RT stage R3 (evidence/rt-r3-bounds.json), built in code.
//   garden           smooth-union hills and trees, a pond, a bench: the painterly target
//   menger_tower     a Mandelbox-folded tower on a plane in light fog: the god-ray target
//   bulb             the Mandelbulb, power 8, on a floor (the site's fractal estimator)
//   arcade           rows of repeated cabinets under a slatted roof in haze: the retro target
//   room_sculptures  blobs, a torus and a column placed in R1's retro_room, for compositing (M6)
#include "raw/renderer/sdf.hpp"
#include <vector>
namespace raw::sdf {

Scene garden();
Scene mengerTower();
Scene bulb();
Scene arcade();
std::vector<Scene> ownedScenes();
Scene roomSculptures();

}  // namespace raw::sdf

#pragma once
// The raster scene as the path tracer's scene (ROADMAP M3 criterion 1; units and checks in
// evidence/m3-verified-scenes-bounds.json). The raster lights a covered pixel with
//   albedo x (0.2 + li max(0, n.l)) x AO                      (src/renderer/composite.cpp)
// so the path tracer gets the same triangles, normals and albedos (each mesh a 1 x 1 texture
// holding its albedo rounded to 8 bits, a pure Lambert material), a constant sky of radiance
// `ambient` (0.2: irradiance 0.2 pi on an open surface, 0.2 x albedo through Lambert) and a sun
// of irradiance pi x li towards -light.dir. sunPi false leaves the pi out: the false control.
#include "raw/renderer/rt_pathtrace.hpp"
#include "raw/scene/scene.hpp"
namespace raw::rt {

PtScene ptFromScene(const Scene& s, float ambient = 0.2f, bool sunPi = true);
// The raster camera as the path tracer's (the same pinhole convention: vertical field of
// view, aspect from the image size).
PtCamera ptCameraOf(const Camera& c);

}  // namespace raw::rt

#pragma once
// Private to the owned-asset generators (src/tools/owned_*.cpp): deterministic mesh building.
// All geometry is computed in double with +, -, *, / and sqrt (correctly rounded everywhere)
// and owned::dsin / dcos, then rounded to float once.
#include "raw/tools/owned_assets.hpp"
#include <functional>
namespace raw::owned::build {

struct P3 { double x{0}, y{0}, z{0}; };
inline P3 operator+(P3 a, P3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline P3 operator-(P3 a, P3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline P3 operator*(P3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline P3 cross(P3 a, P3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3 unit(P3 a);
constexpr double kPi = 3.14159265358979323846;

using Surface = std::function<P3(double u, double v)>;
// A (nu x nv)-quad grid over u, v in [0, 1]; normals from central differences of f, oriented
// along cross(df/du, df/dv) (flip reverses both winding and normals). wrapU joins u = 1 to 0.
void grid(Part& p, const Surface& f, int nu, int nv, bool wrapU = false, bool flip = false);
// An axis-aligned box, flat faces outward.
void box(Part& p, P3 lo, P3 hi);
// A surface of revolution about the vertical axis through `at`: profile (radius, height) points,
// a cap fan where the profile ends off the axis at the top and bottom when capTop/capBottom.
void lathe(Part& p, const std::vector<std::pair<double, double>>& profile, int segments, P3 at, bool capBottom, bool capTop,
           const std::function<double(double angle, double height)>& radiusScale = {});
// Translate every vertex added since `first` by d.
void translate(Part& p, std::size_t first, P3 d);

}  // namespace raw::owned::build

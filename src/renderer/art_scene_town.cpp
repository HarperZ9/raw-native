// P23's three-quarter town (evidence/tiltshift-bounds.json, design.scenes): a street along the x
// axis between two rows of houses, chimneys, lamp posts with emissive sodium globes, a lit window, a
// dog, washing on a line, and people (a cylinder body and a sphere head) along the street and in an
// alley. Owned geometry, traced in float64 by the art scene's primitives (art_scene_prims.hpp).
#include "art_scene_prims.hpp"
#include <cmath>
namespace raw::art {
namespace {
using namespace prims;
constexpr double kGroundHalf = 80.0;
constexpr D3 kCobble{0.30, 0.28, 0.25}, kStreet{0.22, 0.21, 0.20};
constexpr D3 kSodium{24.0, 13.0, 3.2}, kWindowLight{3.0, 1.9, 0.8};

struct HouseSpec { double x0, x1, z0, z1, h; D3 albedo; };
constexpr HouseSpec kHouses[] = {
    // the far row (north, z < 0)
    {-34, -27, -11, -5, 6.0, {0.62, 0.52, 0.40}}, {-26, -19, -11, -5, 8.0, {0.55, 0.50, 0.46}}, {-18, -12, -11, -5, 5.5, {0.66, 0.58, 0.44}},
    {-11, -4, -11, -5, 7.0, {0.48, 0.42, 0.38}}, {-3, 3, -11, -5, 9.0, {0.60, 0.55, 0.50}}, {4, 10, -11, -5, 6.0, {0.58, 0.44, 0.34}},
    {11, 17, -11, -5, 7.5, {0.52, 0.50, 0.44}}, {18, 25, -11, -5, 5.0, {0.64, 0.56, 0.46}}, {26, 33, -11, -5, 6.5, {0.50, 0.44, 0.40}},
    // the near row (south, z > 0), with an alley at x in [-2, 4]
    {-34, -26, 5, 10, 4.0, {0.56, 0.48, 0.40}}, {-25, -17, 5, 10, 5.0, {0.62, 0.56, 0.48}}, {-16, -9, 5, 10, 3.6, {0.50, 0.46, 0.42}},
    {-8, -2, 5, 10, 4.5, {0.60, 0.50, 0.40}}, {4, 11, 5, 10, 4.2, {0.54, 0.50, 0.46}}, {12, 19, 5, 10, 5.0, {0.64, 0.54, 0.42}},
    {20, 28, 5, 10, 3.8, {0.52, 0.46, 0.40}}};
constexpr int kHouseCount = int(sizeof kHouses / sizeof kHouses[0]);   // objects 2 .. 17
constexpr Cylinder kChimneys[] = {{-22.0, -8.0, 0.35, 8.0, 9.4, 18, 2, {0.45, 0.30, 0.26}}, {0.5, -9.0, 0.35, 9.0, 10.3, 19, 2, {0.45, 0.30, 0.26}}};
constexpr Cylinder kPosts[] = {{-10.5, -4.4, 0.09, 0.0, 4.2, 20, 6, {0.10, 0.10, 0.11}}, {0.0, -4.6, 0.09, 0.0, 4.2, 21, 6, {0.10, 0.10, 0.11}},
                               {12.0, 4.4, 0.09, 0.0, 4.2, 22, 6, {0.10, 0.10, 0.11}}};
constexpr Box kWindow{{6.0, 2.2, -5.03}, {7.2, 3.6, -4.98}, 26, 7, {0.2, 0.2, 0.2}, kWindowLight};
constexpr Box kDog{{-6.2, 0.0, 1.2}, {-5.2, 0.5, 1.6}, 27, 8, {0.35, 0.24, 0.16}};
constexpr Box kWashing[] = {{{-1.4, 2.3, 7.4}, {-0.6, 3.0, 7.5}, 28, 9, {0.86, 0.84, 0.78}}, {{0.0, 2.4, 7.4}, {0.9, 3.0, 7.5}, 28, 9, {0.70, 0.76, 0.84}},
                            {{1.4, 2.2, 7.4}, {2.1, 3.0, 7.5}, 28, 9, {0.84, 0.70, 0.62}}, {{2.6, 2.4, 7.4}, {3.4, 3.0, 7.5}, 28, 9, {0.86, 0.84, 0.78}}};
struct PersonSpec { double x, z; D3 albedo; bool street; };
constexpr PersonSpec kPeople[] = {{-13.0, 0.15, {0.20, 0.18, 0.22}, true}, {-7.0, -0.2, {0.42, 0.20, 0.16}, true}, {0.0, 0.0, {0.30, 0.34, 0.40}, true},
                                  {6.0, 0.2, {0.24, 0.30, 0.22}, true}, {12.5, -0.15, {0.50, 0.44, 0.30}, true}, {1.0, 6.2, {0.40, 0.36, 0.34}, false},
                                  {-18.0, 3.4, {0.26, 0.24, 0.30}, false}};
constexpr double kBodyR = 0.24, kBodyTop = 1.45, kHeadR = 0.165;
constexpr D3 kSkin{0.62, 0.46, 0.38};

void ground(const D3& o, const D3& d, Hit& best) {
    if (std::abs(d[1]) < 1e-15) return;
    const double t = -o[1] / d[1];
    const double x = o[0] + t * d[0], z = o[2] + t * d[2];
    if (std::abs(x) <= kGroundHalf && std::abs(z) <= kGroundHalf) consider(best, t, {0, 1, 0}, 1, 1, std::abs(z) < 4.0 ? kStreet : kCobble);
}
}  // namespace

Hit traceTown(const D3& o, const D3& d) {
    Hit best;
    ground(o, d, best);
    for (int i = 0; i < kHouseCount; ++i) {
        const HouseSpec& h = kHouses[i];
        box({{h.x0, 0.0, h.z0}, {h.x1, h.h, h.z1}, 2 + i, 3, h.albedo}, o, d, best);
    }
    for (const Cylinder& c : kChimneys) cylinder(c, o, d, best);
    for (const Cylinder& c : kPosts) cylinder(c, o, d, best);
    for (std::size_t i = 0; i < townLamps().size(); ++i) sphere({townLamps()[i], 0.28, 23 + int(i), 10, {0.9, 0.8, 0.6}, kSodium}, o, d, best);
    box(kWindow, o, d, best);
    box(kDog, o, d, best);
    for (const Box& b : kWashing) box(b, o, d, best);
    for (std::size_t i = 0; i < sizeof kPeople / sizeof kPeople[0]; ++i) {
        const PersonSpec& p = kPeople[i];
        const int obj = 40 + int(i);
        cylinder({p.x, p.z, kBodyR, 0.0, kBodyTop, obj, 11, p.albedo}, o, d, best);
        sphere({{p.x, kPersonHeight - kHeadR, p.z}, kHeadR, obj, 12, kSkin}, o, d, best);
    }
    return best;
}

const std::vector<TownPerson>& townPeople() {
    static const std::vector<TownPerson> v = [] {
        std::vector<TownPerson> o;
        for (std::size_t i = 0; i < sizeof kPeople / sizeof kPeople[0]; ++i) o.push_back({{kPeople[i].x, 0.0, kPeople[i].z}, 40 + int(i), kPeople[i].street});
        return o;
    }();
    return v;
}

const std::vector<TownPoint>& townLiving() {
    static const std::vector<TownPoint> v = {{{-5.7, 0.3, 1.4}, "dog", 0.6}, {{6.6, 2.9, -5.0}, "lit window", 0.5}, {{1.0, 2.7, 7.45}, "washing", 0.4}};
    return v;
}

const std::vector<D3>& townLamps() {
    static const std::vector<D3> v = {{-10.5, 4.45, -4.4}, {0.0, 4.45, -4.6}, {12.0, 4.45, 4.4}};
    return v;
}

}  // namespace raw::art

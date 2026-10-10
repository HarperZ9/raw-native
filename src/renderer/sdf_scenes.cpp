// Owned SDF scenes for RT stage R3: see raw/renderer/sdf_scenes.hpp.
#include "raw/renderer/sdf_scenes.hpp"
#include <cmath>
namespace raw::sdf {
namespace {
void rotY(float a, float r[9]) {
    const float c = std::cos(a), s = std::sin(a);
    const float m[9] = {c, 0, -s, 0, 1, 0, s, 0, c};
    for (int i = 0; i < 9; ++i) r[i] = m[i];
}
void rotX(float a, float r[9]) {
    const float c = std::cos(a), s = std::sin(a);
    const float m[9] = {1, 0, 0, 0, c, s, 0, -s, c};
    for (int i = 0; i < 9; ++i) r[i] = m[i];
}
}  // namespace

// Rolling hills, trees and a pond of smooth-union blobs: the painterly and watercolour target.
Scene garden() {
    Scene s;
    s.name = "garden";
    s.materials = {{{0.36f, 0.52f, 0.28f}}, {{0.45f, 0.32f, 0.2f}}, {{0.3f, 0.55f, 0.32f}}, {{0.55f, 0.62f, 0.75f}}, {{0.82f, 0.68f, 0.45f}}};
    Program& p = s.prog;
    p.plane({0, 1, 0}, 0.0f, 0);
    const float hills[4][4] = {{-3.5f, -2.2f, -6.0f, 3.0f}, {2.5f, -2.6f, -8.0f, 3.5f}, {-0.5f, -3.4f, -12.0f, 4.0f}, {5.0f, -2.0f, -4.0f, 2.6f}};
    for (const auto& h : hills) { p.push({h[0], h[1], h[2]}); p.sphere(h[3], 0); p.pop(); p.op(kSmooth, 0.8f); }
    const float trees[5][3] = {{-1.8f, 0.0f, -4.0f}, {1.2f, 0.0f, -5.5f}, {-3.0f, 0.4f, -8.0f}, {3.2f, 0.3f, -7.0f}, {0.2f, 0.2f, -9.0f}};
    for (const auto& t : trees) {
        p.push({t[0], t[1], t[2]});
        p.capsule({0, 0, 0}, {0, 1.1f, 0}, 0.09f, 1);
        p.push({0, 1.45f, 0}); p.sphere(0.45f, 2); p.pop();
        p.push({0.25f, 1.25f, 0.1f}); p.sphere(0.32f, 2); p.pop();
        p.op(kSmooth, 0.2f);
        p.op(kSmooth, 0.12f);
        p.pop();
        p.op(kUnion);
    }
    p.push({0.6f, 0.02f, -3.2f}); p.cylinder(0.9f, 0.03f, 3); p.pop(); p.op(kUnion);         // the pond
    p.push({-0.6f, 0.25f, -2.6f}); p.roundBox({0.5f, 0.25f, 0.3f}, 0.1f, 4); p.pop(); p.op(kUnion);   // a stone bench
    s.eye = {0.0f, 1.3f, 1.5f}; s.target = {0.0f, 0.8f, -6.0f}; s.fovy = 0.9f; s.tMax = 40.0f;
    s.sunDir = {0.4f, 0.7f, 0.5f};
    return s;
}

// A Mandelbox block floating over a plane, in light fog: the god-ray target.
Scene mengerTower() {
    Scene s;
    s.name = "menger_tower";
    s.materials = {{{0.5f, 0.48f, 0.44f}}, {{0.7f, 0.62f, 0.55f}}};
    Program& p = s.prog;
    p.plane({0, 1, 0}, 0.0f, 0);
    p.push({0.0f, 2.2f, -6.0f}, 0.35f); p.mandelbox(10, -1.8f, 1); p.pop();
    p.op(kUnion);
    s.eye = {3.5f, 1.6f, 2.5f}; s.target = {0.0f, 2.0f, -6.0f}; s.fovy = 0.9f; s.tMax = 40.0f;
    s.sunDir = {-0.45f, 0.55f, -0.7f}; s.fogA = 0.06f; s.fogB = 0.15f; s.fogMax = 30.0f;
    return s;
}

// The Mandelbulb, power 8, on a floor.
Scene bulb() {
    Scene s;
    s.name = "bulb";
    s.materials = {{{0.4f, 0.4f, 0.42f}}, {{0.62f, 0.5f, 0.66f}}};
    Program& p = s.prog;
    p.plane({0, 1, 0}, 1.25f, 0);
    float r[9];
    rotX(-1.5707963f, r);
    p.push({0, 0, 0}, r, 1.0f); p.bulb(8, 8.0f, 1); p.pop();
    p.op(kUnion);
    s.eye = {0.0f, 0.6f, 2.9f}; s.target = {0.0f, 0.0f, 0.0f}; s.fovy = 0.8f; s.tMax = 12.0f;
    s.sunDir = {0.5f, 0.8f, 0.4f};
    return s;
}

// Rows of arcade cabinets under a slatted roof, in haze: the retro target, and god rays.
Scene arcade() {
    Scene s;
    s.name = "arcade";
    s.materials = {{{0.25f, 0.22f, 0.3f}}, {{0.18f, 0.2f, 0.55f}}, {{0.85f, 0.2f, 0.25f}}, {{0.6f, 0.58f, 0.5f}}};
    Program& p = s.prog;
    p.plane({0, 1, 0}, 0.0f, 0);
    p.push({0, 0, 0}); p.repeat({2.0f, 0.0f, 3.0f});
    p.push({0, 0.9f, 0}); p.roundBox({0.45f, 0.9f, 0.4f}, 0.05f, 1); p.pop();
    p.capsule({0.0f, 1.05f, 0.45f}, {0.0f, 1.25f, 0.45f}, 0.05f, 2);
    p.op(kUnion);
    p.pop();
    p.push({0, 0, -6.0f}); p.box({7.0f, 4.0f, 9.0f}, 1); p.pop();                    // the hall's bounds
    p.op(kIntersect);
    p.op(kUnion);
    p.push({0, 4.2f, -6.0f}); p.box({7.5f, 0.15f, 9.5f}, 3); p.pop();                // the roof
    p.push({0, 4.2f, -6.0f}); p.repeat({1.2f, 0.0f, 0.0f}); p.box({0.3f, 0.4f, 8.5f}, 3); p.pop();   // its slats cut
    p.op(kSubtract);
    p.op(kUnion);
    s.eye = {5.5f, 2.2f, 3.0f}; s.target = {0.0f, 1.0f, -6.0f}; s.fovy = 0.95f; s.tMax = 40.0f;
    s.sunDir = {0.25f, 0.9f, 0.35f}; s.fogA = 0.12f; s.fogB = 0.25f; s.fogMax = 25.0f;
    return s;
}

std::vector<Scene> ownedScenes() { return {garden(), mengerTower(), bulb(), arcade()}; }

// Sculptures for the retro room (M6): smooth blobs on the table, a floating torus, a column.
Scene roomSculptures() {
    Scene s;
    s.name = "room_sculptures";
    s.materials = {{{0.8f, 0.55f, 0.3f}}, {{0.35f, 0.6f, 0.75f}}, {{0.7f, 0.7f, 0.68f}}};
    Program& p = s.prog;
    p.push({-1.5f, 1.05f, -5.1f}); p.sphere(0.2f, 0);
    p.push({0.18f, -0.05f, 0.05f}); p.sphere(0.14f, 0); p.pop(); p.op(kSmooth, 0.1f);
    p.push({-0.15f, 0.12f, -0.05f}); p.sphere(0.11f, 0); p.pop(); p.op(kSmooth, 0.1f);
    p.pop();
    float r[9];
    rotY(0.6f, r);
    p.push({1.8f, 1.4f, -8.0f}, r, 1.0f); p.torus(0.5f, 0.12f, 1); p.pop(); p.op(kUnion);
    p.capsule({-2.8f, 0.0f, -9.0f}, {-2.8f, 2.2f, -9.0f}, 0.25f, 2); p.op(kUnion);
    s.eye = {0, 1.6f, 1.0f}; s.target = {0.3f, 1.2f, -8.0f}; s.fovy = 1.1f; s.tMax = 100.0f;
    return s;
}

}  // namespace raw::sdf

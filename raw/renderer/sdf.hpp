#pragma once
// raw-native's ray marcher (RT stage R3, evidence/rt-r3-bounds.json): an SDF scene graph as a
// postfix program, sphere tracing, soft shadows, ambient occlusion, fog and god rays. This CPU
// form evaluates in float64 and is the reference for src/renderer/gpu/shaders/sdf.wgsl, which
// runs the same program in float32.
//
// A program is a list of nodes, kNodeFloats floats each: op, material, then parameters. The
// evaluator keeps a stack of (distance, material) and a stack of query frames (point, scale).
#include "raw/math/vec.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::sdf {

inline constexpr int kNodeFloats = 16;
inline constexpr int kStack = 16;
enum Op : int {
    kSphere = 1, kBox, kRoundBox, kTorus, kCapsule, kCylinder, kPlane, kBulb, kMandelbox,   // primitives push
    kUnion = 20, kSmooth, kSubtract, kIntersect,                                           // binary ops pop two, push one
    kPushFrame = 30, kPopFrame, kRepeat,                                                   // query-point frames
};

struct D3 { double x, y, z; };
inline D3 d3(Vec3 v) { return {v.x, v.y, v.z}; }

struct Program {
    std::vector<float> nodes;
    bool fractal{false};      // holds a fractal or a smooth union: the march steps 0.6 d
    // Builders. Primitive parameters are in the current frame.
    void sphere(float r, int mat);
    void box(Vec3 half, int mat);
    void roundBox(Vec3 half, float r, int mat);
    void torus(float R, float r, int mat);
    void capsule(Vec3 a, Vec3 b, float r, int mat);
    void cylinder(float r, float h, int mat);
    void plane(Vec3 n, float h, int mat);
    void bulb(int iterations, float power, int mat);
    void mandelbox(int iterations, float scale, int mat);
    void op(Op o, float k = 0.0f);
    // A frame: p' = R (p - t) / s, distances scaled back by s. rot is row-major 3 x 3.
    void push(Vec3 t, const float rot[9], float s);
    void push(Vec3 t, float s = 1.0f);
    void pop();
    void repeat(Vec3 period);   // 0 on an axis: no repetition
};

struct Material { Vec3 albedo; };
struct Scene {
    std::string name;
    Program prog;
    std::vector<Material> materials;
    Vec3 eye, target, up{0, 1, 0};
    float fovy{1.0f}, tMax{60.0f};
    Vec3 sunDir{0.5f, 0.8f, 0.3f};
    // Height fog density a exp(-b y), scattering sigma_s = density; god rays use the sun.
    float fogA{0.0f}, fogB{0.3f}, fogMax{40.0f};
};

struct Sample { double d; int mat; };
Sample eval(const Program& p, D3 q);

struct MarchHit { bool hit{false}; double t{0}; int mat{-1}; D3 n{0, 0, 0}; double closest{1e30}; int steps{0}; };
// Sphere tracing from o along unit d; closest is the smallest d / (1 + t) seen (for the
// grazing exemption of M1 and M2). eps scales the hit test (1e-4 normally; M1's control 1e-2).
// refine: the sign-bracketed refinement of method notes 1 and 2 (off only for M1's control).
MarchHit march(const Program& p, D3 o, D3 d, double tMax, double eps = 1e-4, bool refine = true);
D3 normal(const Program& p, D3 x, double t);
double softShadow(const Program& p, D3 x, D3 l, double k = 12.0);
double ambientOcclusion(const Program& p, D3 x, D3 n);
bool hardShadow(const Program& p, D3 x, D3 l);   // true when the sun is blocked

// Fog (M4): closed forms and the numerical march of the height fog.
double fogHomogeneous(double sigma, double t);
double fogHeight(double a, double b, D3 o, D3 d, double t);
double fogHeightMarch(double a, double b, D3 o, D3 d, double t, int samples);

// God rays (M5): single scattering along the view ray; visible receives the number of
// samples whose sun ray is clear.
double godRays(const Scene& s, D3 o, D3 d, double tEnd, int& visible);
double henyeyGreenstein(double cosTheta, double g);

// Camera rays as the GPU makes them.
D3 cameraRay(const Scene& s, int x, int y, int w, int h);

}  // namespace raw::sdf

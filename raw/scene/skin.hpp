#pragma once
// The CPU skinning and morph reference (ROADMAP M4 criterion 3, evidence/m4-skinning-cpu-bounds.json).
// A port of the JavaScript oracle web/anim/skeleton.mjs in the same operation order: float64,
// column-major 4 x 4 matrices (element (r, c) at m[c * 4 + r]), quaternions (x, y, z, w).
// GPU skinning and the glTF skin import are checked against this.
//
// A joint's rest frame is its parent's frame times compose(t, r). A pose adds a local rotation
// after the rest rotation and a translation offset. The bind pose is the rest pose, so skinning
// with an empty pose returns the mesh. Morph targets (glTF 2.0) are applied before skinning.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::anim {

using Mat = std::array<double, 16>;
using Vec3d = std::array<double, 3>;
using Quat = std::array<double, 4>;

struct JointDesc {
    std::string name;
    int parent{-1};                 // index of an earlier joint, -1 for a root
    Vec3d t{0, 0, 0};
    Quat r{0, 0, 0, 1};
};
struct Skeleton {
    std::vector<JointDesc> joints;
    std::vector<Mat> bind, invBind;
};
struct JointPose {
    bool hasR{false}, hasT{false};
    Quat r{0, 0, 0, 1};
    Vec3d t{0, 0, 0};
};
// One entry a joint, or empty for the rest pose.
using Pose = std::vector<JointPose>;

struct SkinMesh {
    std::vector<float> pos, nrm;              // 3 a vertex; nrm may be empty
    std::vector<std::uint16_t> joints;        // 4 a vertex
    std::vector<float> weights;               // 4 a vertex, renormalised by skin()
};
struct SkinOutput { std::vector<float> pos, nrm; };
struct MorphTarget { std::vector<float> dpos, dnrm; };   // 3 a vertex each; either may be empty

// False controls (evidence k6); all false for the reference.
struct SkinControls {
    bool skipRenormalise{false};          // weights used as given
    bool normalsWithTranslation{false};   // normals transformed as points
};

Mat compose(const Vec3d& t, const Quat& r);
Mat mul(const Mat& a, const Mat& b);
Vec3d apply(const Mat& m, const Vec3d& p);
Mat rigidInverse(const Mat& m);
Quat quatMul(const Quat& a, const Quat& b);
Quat quatAxisAngle(const Vec3d& axis, double angle);

// Refuses (err set, empty skeleton) a parent defined after its child and a duplicate name.
Skeleton makeSkeleton(const std::vector<JointDesc>& joints, std::string& err);
std::vector<Mat> poseSkeleton(const Skeleton& sk, const Pose& pose);
std::vector<Mat> skinMatrices(const std::vector<Mat>& world, const std::vector<Mat>& invBind);
// Linear-blend skinning, four influences a vertex. Refuses a joint index out of range.
SkinOutput skin(const SkinMesh& mesh, const std::vector<Mat>& M, std::string& err, const SkinControls& c = {});
// base + sum w_i d_i; normals renormalised. Refuses a weight count that differs from the target
// count and a target whose size differs from the mesh's.
SkinMesh morph(const SkinMesh& base, const std::vector<MorphTarget>& targets, const std::vector<float>& weights, std::string& err);

}  // namespace raw::anim

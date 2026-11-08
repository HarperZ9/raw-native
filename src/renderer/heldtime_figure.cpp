// The demo figure: skeleton, clip, skinned pose buffer. See raw/renderer/heldtime_scene.hpp.
#include "raw/renderer/heldtime_scene.hpp"
#include <cmath>
#include <stdexcept>
namespace raw::heldtime {
namespace {
using anim::JointDesc;
// root, hips, chest, head, shoulderL, handL, shoulderR, handR, footL, footR
const std::vector<JointDesc> kJoints = {
    {"root", -1, {0, 0, 0}, {0, 0, 0, 1}},       {"hips", 0, {0, 0.6, 0}, {0, 0, 0, 1}},
    {"chest", 1, {0, 0.5, 0}, {0, 0, 0, 1}},     {"head", 2, {0, 0.38, 0}, {0, 0, 0, 1}},
    {"shoulderL", 2, {0.3, 0, 0}, {0, 0, 0, 1}}, {"handL", 4, {0, -0.48, 0}, {0, 0, 0, 1}},
    {"shoulderR", 2, {-0.3, 0, 0}, {0, 0, 0, 1}}, {"handR", 6, {0, -0.48, 0}, {0, 0, 0, 1}},
    {"footL", 0, {0.16, 0.02, 0.05}, {0, 0, 0, 1}}, {"footR", 0, {-0.16, 0.02, -0.05}, {0, 0, 0, 1}}};
// Bones the skinned points lie on (parent, child).
const int kBones[][2] = {{1, 2}, {2, 3}, {4, 5}, {6, 7}, {1, 8}, {1, 9}};

anim::Skeleton build() {
    std::string err;
    anim::Skeleton s = anim::makeSkeleton(kJoints, err);
    if (!err.empty()) throw std::logic_error("heldtime: " + err);
    return s;
}
std::array<double, 3> pos(const anim::Mat& m) { return {m[12], m[13], m[14]}; }
}  // namespace

anim::Clip demoClip() {
    // channels: hop, lean (hips about z), twist (chest about y), nod (head about x), armL, armR (about z)
    anim::Clip c{4.0, true, {}};
    c.keys = {{0.0, {0, 0, 0, 0, 0.3, -0.3}},          {0.5, {0.22, 0.12, 0.3, 0.2, 2.4, -0.5}},
              {1.0, {0, -0.1, 0, -0.1, 0.5, -0.4}},     {1.5, {0.18, -0.15, -0.35, 0.25, 0.4, -2.3}},
              {2.0, {0, 0.1, 0, 0, 0.3, -0.3}},         {2.5, {0.25, 0.2, 0.5, 0.3, 2.6, -2.6}},
              {3.0, {0, 0, -0.2, -0.15, 0.6, -0.6}},    {3.5, {0.1, -0.1, 0.2, 0.1, 1.2, -1.0}}};
    return c;
}

const anim::Skeleton& demoSkeleton() { static const anim::Skeleton s = build(); return s; }

const anim::SkinMesh& demoMesh() {
    static const anim::SkinMesh m = [] {
        anim::SkinMesh mesh;
        const std::vector<anim::Mat> rest = anim::poseSkeleton(demoSkeleton(), {});
        for (const auto& b : kBones) for (int k = 0; k < 6; ++k) {
            const double u = (k + 0.5) / 6.0;
            const auto a = pos(rest[std::size_t(b[0])]), c = pos(rest[std::size_t(b[1])]);
            for (int i = 0; i < 3; ++i) mesh.pos.push_back(float(a[std::size_t(i)] + (c[std::size_t(i)] - a[std::size_t(i)]) * u));
            mesh.joints.insert(mesh.joints.end(), {std::uint16_t(b[0]), std::uint16_t(b[1]), 0, 0});
            mesh.weights.insert(mesh.weights.end(), {float(1 - u), float(u), 0.0f, 0.0f});
        }
        return mesh;
    }();
    return m;
}

Figure figureAt(double clipTime) {
    const std::vector<double> ch = anim::sampleClip(demoClip(), clipTime);
    anim::Pose pose(kJoints.size());
    pose[0].hasT = true; pose[0].t = {0, ch[0], 0};
    pose[1].hasR = true; pose[1].r = anim::quatAxisAngle({0, 0, 1}, ch[1]);
    pose[2].hasR = true; pose[2].r = anim::quatAxisAngle({0, 1, 0}, ch[2]);
    pose[3].hasR = true; pose[3].r = anim::quatAxisAngle({1, 0, 0}, ch[3]);
    pose[4].hasR = true; pose[4].r = anim::quatAxisAngle({0, 0, 1}, ch[4]);
    pose[6].hasR = true; pose[6].r = anim::quatAxisAngle({0, 0, 1}, ch[5]);
    const anim::Skeleton& sk = demoSkeleton();
    const std::vector<anim::Mat> world = anim::poseSkeleton(sk, pose);
    std::string err;
    const anim::SkinOutput sk_out = anim::skin(demoMesh(), anim::skinMatrices(world, sk.invBind), err);
    if (!err.empty()) throw std::logic_error("heldtime: " + err);
    Figure f;
    for (const anim::Mat& m : world) {
        f.joints.push_back(pos(m));
        for (double v : pos(m)) f.poseBuffer.push_back(float(v));
    }
    f.poseBuffer.insert(f.poseBuffer.end(), sk_out.pos.begin(), sk_out.pos.end());
    return f;
}

}  // namespace raw::heldtime

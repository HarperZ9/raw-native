// The C++ CPU skinning and morph reference (evidence/m4-skinning-cpu-bounds.json, k1 to k7):
// parity with the JavaScript oracle on tests/data/skin-fixture.txt, analytic checks and two
// false controls that must miss the parity bound.
#include "raw/scene/skin.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace raw::anim;

namespace {
struct Fixture {
    std::vector<JointDesc> joints;
    std::vector<Pose> poses;
    SkinMesh mesh;
    std::vector<std::vector<float>> pos, nrm;   // the oracle's output per pose
    bool ok{false};
};
Fixture readFixture(const std::string& path) {
    Fixture f;
    std::ifstream in(path);
    std::string tag; int ver = 0, n = 0, p = 0, v = 0;
    if (!(in >> tag >> ver) || tag != "skin-fixture" || ver != 1) return f;
    in >> tag >> n;
    for (int i = 0; i < n; ++i) {
        JointDesc j; j.name = "j" + std::to_string(i);
        in >> j.parent >> j.t[0] >> j.t[1] >> j.t[2] >> j.r[0] >> j.r[1] >> j.r[2] >> j.r[3];
        f.joints.push_back(j);
    }
    in >> tag >> p;
    for (int k = 0; k < p; ++k) {
        Pose pose; pose.resize(std::size_t(n));
        for (int i = 0; i < n; ++i) {
            JointPose& e = pose[std::size_t(i)];
            int hr = 0, ht = 0;
            in >> hr >> e.r[0] >> e.r[1] >> e.r[2] >> e.r[3] >> ht >> e.t[0] >> e.t[1] >> e.t[2];
            e.hasR = hr != 0; e.hasT = ht != 0;
        }
        f.poses.push_back(pose);
    }
    in >> tag >> v;
    f.mesh.pos.resize(std::size_t(v) * 3); f.mesh.nrm.resize(std::size_t(v) * 3);
    f.mesh.joints.resize(std::size_t(v) * 4); f.mesh.weights.resize(std::size_t(v) * 4);
    for (int i = 0; i < v; ++i) {
        double d;
        for (int c = 0; c < 3; ++c) { in >> d; f.mesh.pos[std::size_t(i) * 3 + c] = float(d); }
        for (int c = 0; c < 3; ++c) { in >> d; f.mesh.nrm[std::size_t(i) * 3 + c] = float(d); }
        for (int c = 0; c < 4; ++c) { int j; in >> j; f.mesh.joints[std::size_t(i) * 4 + c] = std::uint16_t(j); }
        for (int c = 0; c < 4; ++c) { in >> d; f.mesh.weights[std::size_t(i) * 4 + c] = float(d); }
    }
    for (int k = 0; k < p; ++k) {
        std::vector<float> a(std::size_t(v) * 3), b(std::size_t(v) * 3);
        for (int i = 0; i < v; ++i) {
            double d;
            for (int c = 0; c < 3; ++c) { in >> d; a[std::size_t(i) * 3 + c] = float(d); }
            for (int c = 0; c < 3; ++c) { in >> d; b[std::size_t(i) * 3 + c] = float(d); }
        }
        f.pos.push_back(a); f.nrm.push_back(b);
    }
    f.ok = bool(in);
    return f;
}
double maxAbs(const std::vector<float>& a, const std::vector<float>& b) {
    double m = a.size() == b.size() ? 0.0 : 1e30;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::fabs(double(a[i]) - double(b[i])));
    return m;
}
double dist(const Mat& a, const Mat& b) {
    return std::sqrt((a[12] - b[12]) * (a[12] - b[12]) + (a[13] - b[13]) * (a[13] - b[13]) + (a[14] - b[14]) * (a[14] - b[14]));
}
}  // namespace

int main() {
    const Fixture f = readFixture(std::string(RAW_SOURCE_DIR) + "/tests/data/skin-fixture.txt");
    CHECK(f.ok && f.joints.size() == 12 && f.poses.size() == 3 && f.mesh.pos.size() == 1200);
    std::string err;
    const Skeleton sk = makeSkeleton(f.joints, err);
    CHECK(err.empty() && sk.joints.size() == 12);

    // k1: parity with the JavaScript oracle, every vertex of every pose.
    double worstP = 0, worstN = 0;
    for (std::size_t k = 0; k < f.poses.size(); ++k) {
        const SkinOutput o = skin(f.mesh, skinMatrices(poseSkeleton(sk, f.poses[k]), sk.invBind), err);
        CHECK(err.empty());
        worstP = std::max(worstP, maxAbs(o.pos, f.pos[k]));
        worstN = std::max(worstN, maxAbs(o.nrm, f.nrm[k]));
    }
    std::printf("k1 js parity: max abs pos %.3g nrm %.3g (bound 2e-7)\n", worstP, worstN);
    CHECK(worstP <= 2e-7 && worstN <= 2e-7);

    // k6: the false controls miss the k1 bound.
    {
        SkinControls noNorm; noNorm.skipRenormalise = true;
        SkinControls fullNrm; fullNrm.normalsWithTranslation = true;
        const std::vector<Mat> M = skinMatrices(poseSkeleton(sk, f.poses[0]), sk.invBind);
        const SkinOutput a = skin(f.mesh, M, err, noNorm), b = skin(f.mesh, M, err, fullNrm);
        std::printf("k6 controls: unnormalised weights pos %.3g, translated normals nrm %.3g\n", maxAbs(a.pos, f.pos[0]), maxAbs(b.nrm, f.nrm[0]));
        CHECK(maxAbs(a.pos, f.pos[0]) > 2e-7);
        CHECK(maxAbs(b.nrm, f.nrm[0]) > 2e-7);
    }

    // k2: the bind pose returns the mesh.
    {
        const SkinOutput o = skin(f.mesh, skinMatrices(poseSkeleton(sk, Pose{}), sk.invBind), err);
        CHECK(maxAbs(o.pos, f.mesh.pos) <= 1e-6 && maxAbs(o.nrm, f.mesh.nrm) <= 1e-6);
    }

    // k3: one influence is rigid; half and half is the midpoint.
    {
        const std::vector<Mat> M = skinMatrices(poseSkeleton(sk, f.poses[1]), sk.invBind);
        SkinMesh m;
        m.pos = {0.1f, 0.2f, 0.3f, 0.1f, 0.2f, 0.3f};
        m.joints = {3, 0, 0, 0, 3, 9, 0, 0};
        m.weights = {1, 0, 0, 0, 0.5f, 0.5f, 0, 0};
        const SkinOutput o = skin(m, M, err);
        const Vec3d p{0.1f, 0.2f, 0.3f}, a = apply(M[3], p), b = apply(M[9], p);
        for (int c = 0; c < 3; ++c) {
            CHECK(std::fabs(o.pos[std::size_t(c)] - a[std::size_t(c)]) <= 1e-6);
            CHECK(std::fabs(o.pos[3 + std::size_t(c)] - 0.5 * (a[std::size_t(c)] + b[std::size_t(c)])) <= 1e-6);
        }
        CHECK(o.nrm.empty());   // no normals in, no normals out
    }

    // k4: bone lengths are invariant (only the root carries a translation offset).
    {
        const std::vector<Mat> rest = poseSkeleton(sk, Pose{});
        double worst = 0;
        for (const Pose& pose : f.poses) {
            const std::vector<Mat> w = poseSkeleton(sk, pose);
            for (std::size_t i = 1; i < sk.joints.size(); ++i) {
                const std::size_t par = std::size_t(sk.joints[i].parent);
                worst = std::max(worst, std::fabs(dist(w[i], w[par]) - dist(rest[i], rest[par])));
            }
        }
        CHECK(worst <= 1e-12);
    }

    // k5: morph targets, applied before skinning.
    {
        SkinMesh base;
        base.pos = {1, 2, 3, -1, 0.5f, 0};
        base.nrm = {0, 0, 1, 0, 1, 0};
        MorphTarget t0{{0.25f, 0, -1, 0, 0, 0}, {1, 0, 0, 0, 0, 0}}, t1{{0, 0, 0, 2, 2, 2}, {}};
        const std::vector<MorphTarget> targets{t0, t1};
        CHECK(maxAbs(morph(base, targets, {0, 0}, err).pos, base.pos) == 0.0 && err.empty());
        const SkinMesh one = morph(base, targets, {1, 0}, err);
        CHECK(maxAbs(one.pos, {1.25f, 2, 2, -1, 0.5f, 0}) <= 1e-6);
        CHECK(std::fabs(one.nrm[0] - std::sqrt(0.5)) <= 1e-6 && std::fabs(one.nrm[2] - std::sqrt(0.5)) <= 1e-6);   // renormalised
        const SkinMesh half = morph(base, targets, {0, 0.5f}, err);
        CHECK(maxAbs(half.pos, {1, 2, 3, 0, 1.5f, 1}) <= 1e-6);
        (void)morph(base, targets, {1}, err);
        CHECK(!err.empty());   // one weight per target
        err.clear();
        MorphTarget bad{{1, 2}, {}};
        (void)morph(base, {bad}, {1}, err);
        CHECK(!err.empty());   // a target the size of the mesh
        err.clear();
    }

    // k7: refusals.
    {
        std::string e1, e2, e3;
        (void)makeSkeleton({{"knee", 1, {0, 0, 0}, {0, 0, 0, 1}}, {"hip", -1, {0, 0, 0}, {0, 0, 0, 1}}}, e1);
        CHECK(e1.find("before") != std::string::npos);
        (void)makeSkeleton({{"a", -1, {0, 0, 0}, {0, 0, 0, 1}}, {"a", 0, {0, 0, 0}, {0, 0, 0, 1}}}, e2);
        CHECK(e2.find("duplicate") != std::string::npos);
        SkinMesh m; m.pos = {0, 0, 0}; m.joints = {40, 0, 0, 0}; m.weights = {1, 0, 0, 0};
        (void)skin(m, skinMatrices(poseSkeleton(sk, Pose{}), sk.invBind), e3);
        CHECK(e3.find("range") != std::string::npos);
    }
    return raw_test_summary();
}

// The CPU skinning and morph reference: see raw/scene/skin.hpp. Each function repeats the
// operation order of web/anim/skeleton.mjs and web/world/scene.mjs.
#include "raw/scene/skin.hpp"
#include <cmath>
#include <set>
namespace raw::anim {

Mat compose(const Vec3d& t, const Quat& r) {
    const double x = r[0], y = r[1], z = r[2], w = r[3];
    const double xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    Mat m{};
    m[0] = (1 - 2 * (yy + zz)) * 1.0; m[1] = 2 * (xy + wz) * 1.0; m[2] = 2 * (xz - wy) * 1.0;
    m[4] = 2 * (xy - wz) * 1.0; m[5] = (1 - 2 * (xx + zz)) * 1.0; m[6] = 2 * (yz + wx) * 1.0;
    m[8] = 2 * (xz + wy) * 1.0; m[9] = 2 * (yz - wx) * 1.0; m[10] = (1 - 2 * (xx + yy)) * 1.0;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
    return m;
}
Mat mul(const Mat& a, const Mat& b) {
    Mat o{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            double v = 0;
            for (int k = 0; k < 4; ++k) v += a[std::size_t(k * 4 + r)] * b[std::size_t(c * 4 + k)];
            o[std::size_t(c * 4 + r)] = v;
        }
    return o;
}
Vec3d apply(const Mat& m, const Vec3d& p) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}
Mat rigidInverse(const Mat& m) {
    Mat o{};
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) o[std::size_t(c * 4 + r)] = m[std::size_t(r * 4 + c)];
    for (int r = 0; r < 3; ++r) o[std::size_t(12 + r)] = -(o[std::size_t(r)] * m[12] + o[std::size_t(4 + r)] * m[13] + o[std::size_t(8 + r)] * m[14]);
    o[15] = 1;
    return o;
}
Quat quatMul(const Quat& a, const Quat& b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
Quat quatAxisAngle(const Vec3d& axis, double angle) {
    double l = std::hypot(axis[0], axis[1], axis[2]);
    if (l == 0) l = 1;
    const double s = std::sin(angle / 2);
    return {(axis[0] / l) * s, (axis[1] / l) * s, (axis[2] / l) * s, std::cos(angle / 2)};
}

Skeleton makeSkeleton(const std::vector<JointDesc>& joints, std::string& err) {
    Skeleton sk;
    std::set<std::string> names;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const JointDesc& j = joints[i];
        if (!names.insert(j.name).second) { err = "skeleton: duplicate joint " + j.name; return {}; }
        if (j.parent >= int(i) || j.parent < -1) { err = "skeleton: joint " + j.name + " names a parent that is not defined before it"; return {}; }
        sk.joints.push_back(j);
    }
    sk.bind = poseSkeleton(sk, Pose{});
    for (const Mat& m : sk.bind) sk.invBind.push_back(rigidInverse(m));
    return sk;
}
std::vector<Mat> poseSkeleton(const Skeleton& sk, const Pose& pose) {
    std::vector<Mat> out(sk.joints.size());
    for (std::size_t i = 0; i < sk.joints.size(); ++i) {
        const JointDesc& j = sk.joints[i];
        const JointPose p = i < pose.size() ? pose[i] : JointPose{};
        const Vec3d t = p.hasT ? Vec3d{j.t[0] + p.t[0], j.t[1] + p.t[1], j.t[2] + p.t[2]} : j.t;
        const Mat local = compose(t, p.hasR ? quatMul(j.r, p.r) : j.r);
        out[i] = j.parent < 0 ? local : mul(out[std::size_t(j.parent)], local);
    }
    return out;
}
std::vector<Mat> skinMatrices(const std::vector<Mat>& world, const std::vector<Mat>& invBind) {
    std::vector<Mat> out(world.size());
    for (std::size_t i = 0; i < world.size(); ++i) out[i] = mul(world[i], invBind[i]);
    return out;
}

SkinOutput skin(const SkinMesh& mesh, const std::vector<Mat>& M, std::string& err, const SkinControls& ctl) {
    const std::size_t n = mesh.pos.size() / 3;
    const bool hasN = !mesh.nrm.empty();
    SkinOutput o;
    if (mesh.joints.size() < n * 4 || mesh.weights.size() < n * 4 || (hasN && mesh.nrm.size() < n * 3)) { err = "skin: joints, weights or normals shorter than the vertex count"; return o; }
    for (std::size_t k = 0; k < n * 4; ++k)
        if (mesh.weights[k] != 0 && mesh.joints[k] >= M.size()) { err = "skin: joint index " + std::to_string(mesh.joints[k]) + " out of range"; return o; }
    o.pos.resize(n * 3);
    if (hasN) o.nrm.resize(n * 3);
    for (std::size_t v = 0; v < n; ++v) {
        double sw = 0;
        for (int k = 0; k < 4; ++k) sw += mesh.weights[v * 4 + std::size_t(k)];
        if (sw <= 0) {
            for (int c = 0; c < 3; ++c) { o.pos[v * 3 + std::size_t(c)] = mesh.pos[v * 3 + std::size_t(c)]; if (hasN) o.nrm[v * 3 + std::size_t(c)] = mesh.nrm[v * 3 + std::size_t(c)]; }
            continue;
        }
        if (ctl.skipRenormalise) sw = 1;
        const Vec3d p{mesh.pos[v * 3], mesh.pos[v * 3 + 1], mesh.pos[v * 3 + 2]};
        double x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0;
        for (int k = 0; k < 4; ++k) {
            const double w = mesh.weights[v * 4 + std::size_t(k)] / sw;
            if (w == 0) continue;
            const Mat& m = M[mesh.joints[v * 4 + std::size_t(k)]];
            const Vec3d q = apply(m, p);
            x += w * q[0]; y += w * q[1]; z += w * q[2];
            if (hasN) {
                const Vec3d d{mesh.nrm[v * 3], mesh.nrm[v * 3 + 1], mesh.nrm[v * 3 + 2]};
                const double tx = ctl.normalsWithTranslation ? m[12] : 0, ty = ctl.normalsWithTranslation ? m[13] : 0, tz = ctl.normalsWithTranslation ? m[14] : 0;
                nx += w * (m[0] * d[0] + m[4] * d[1] + m[8] * d[2] + tx);
                ny += w * (m[1] * d[0] + m[5] * d[1] + m[9] * d[2] + ty);
                nz += w * (m[2] * d[0] + m[6] * d[1] + m[10] * d[2] + tz);
            }
        }
        o.pos[v * 3] = float(x); o.pos[v * 3 + 1] = float(y); o.pos[v * 3 + 2] = float(z);
        if (hasN) {
            double l = std::hypot(nx, ny, nz);
            if (l == 0) l = 1;
            o.nrm[v * 3] = float(nx / l); o.nrm[v * 3 + 1] = float(ny / l); o.nrm[v * 3 + 2] = float(nz / l);
        }
    }
    return o;
}

SkinMesh morph(const SkinMesh& base, const std::vector<MorphTarget>& targets, const std::vector<float>& weights, std::string& err) {
    SkinMesh out = base;
    if (weights.size() != targets.size()) { err = "morph: " + std::to_string(weights.size()) + " weights for " + std::to_string(targets.size()) + " targets"; return out; }
    for (const MorphTarget& t : targets)
        if ((!t.dpos.empty() && t.dpos.size() != base.pos.size()) || (!t.dnrm.empty() && t.dnrm.size() != base.nrm.size())) { err = "morph: a target's size differs from the mesh's"; return out; }
    for (std::size_t i = 0; i < base.pos.size(); ++i) {
        double p = base.pos[i];
        for (std::size_t k = 0; k < targets.size(); ++k) if (!targets[k].dpos.empty()) p += double(weights[k]) * targets[k].dpos[i];
        out.pos[i] = float(p);
    }
    for (std::size_t v = 0; v * 3 < base.nrm.size(); ++v) {
        double d[3];
        for (int c = 0; c < 3; ++c) {
            const std::size_t i = v * 3 + std::size_t(c);
            d[c] = base.nrm[i];
            for (std::size_t k = 0; k < targets.size(); ++k) if (!targets[k].dnrm.empty()) d[c] += double(weights[k]) * targets[k].dnrm[i];
        }
        double l = std::hypot(d[0], d[1], d[2]);
        if (l == 0) l = 1;
        for (int c = 0; c < 3; ++c) out.nrm[v * 3 + std::size_t(c)] = float(d[c] / l);
    }
    return out;
}

}  // namespace raw::anim

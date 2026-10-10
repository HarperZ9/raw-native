// glTF 2.0 import; see raw/assets/gltf.hpp. Written from the Khronos glTF 2.0
// specification; no third-party loader.
#include "raw/assets/gltf.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_set>
namespace raw::assets {
namespace {
constexpr std::int64_t kMaxElements = std::int64_t(1) << 26;      // per accessor
constexpr std::int64_t kMaxBytes = std::int64_t(1) << 30;
constexpr int kMaxNodeDepth = 128;

[[noreturn]] void fail(const std::string& m){ throw AssetError("glTF: " + m); }

std::uint32_t le32(const std::uint8_t* p){ return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24; }

std::vector<std::uint8_t> base64(std::string_view s){
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+') return 62; if (c == '/') return 63; return -1; };
    std::vector<std::uint8_t> out;
    out.reserve(s.size() / 4 * 3);
    std::uint32_t acc = 0; int bits = 0;
    for (const char c : s){
        if (c == '=') break;
        const int v = val(c);
        if (v < 0) fail("bad base64 in a data uri");
        acc = (acc << 6) | std::uint32_t(v); bits += 6;
        if (bits >= 8){ bits -= 8; out.push_back(std::uint8_t(acc >> bits)); }
    }
    return out;
}

struct Doc {
    const Json& j;
    std::vector<std::vector<std::uint8_t>> buffers;
    const Json& arr(const char* key) const {
        static const Json empty{Json::Kind::Array};
        const Json* a = j.get(key);
        if (!a) return empty;
        if (!a->is(Json::Kind::Array)) fail(std::string(key) + " is not an array");
        return *a;
    }
    const Json& item(const char* key, std::int64_t i) const {
        const Json& a = arr(key);
        if (i < 0 || std::size_t(i) >= a.a.size()) fail(std::string(key) + " index " + std::to_string(i) + " out of range");
        const Json& e = a.a[std::size_t(i)];
        if (!e.is(Json::Kind::Object)) fail(std::string(key) + " entry is not an object");
        return e;
    }
};

std::int64_t intOf(const Json& o, const char* key, std::int64_t def, std::int64_t lo, std::int64_t hi){
    const Json* v = o.get(key);
    return v ? v->integer(key, lo, hi) : def;
}
// A required integer: missing is an error (found by the fuzzer: a default of -1 used to index).
std::int64_t need(const Json& o, const char* key, std::int64_t lo, std::int64_t hi){
    const Json* v = o.get(key);
    if (!v) fail(std::string("missing ") + key);
    return v->integer(key, lo, hi);
}

// An accessor's elements as floats (comps per element) or as unsigned indices.
struct View { const std::uint8_t* base; std::int64_t count, stride; int comps, type; };
View accessor(const Doc& d, std::int64_t idx){
    const Json& acc = d.item("accessors", idx);
    if (acc.get("sparse")) fail("sparse accessors are not supported");
    const std::int64_t type = need(acc, "componentType", 0, 1 << 20);
    const Json* t = acc.get("type");
    if (!t || !t->is(Json::Kind::String)) fail("accessor without a type");
    const int comps = t->s == "SCALAR" ? 1 : t->s == "VEC2" ? 2 : t->s == "VEC3" ? 3 : t->s == "VEC4" ? 4 : t->s == "MAT4" ? 16 : 0;
    if (!comps) fail("accessor type " + t->s.substr(0, 16) + " is not supported");
    const int csize = type == 5126 || type == 5125 ? 4 : type == 5123 || type == 5122 ? 2 : type == 5121 || type == 5120 ? 1 : 0;
    if (!csize) fail("componentType " + std::to_string(type) + " is not supported");
    const std::int64_t count = need(acc, "count", 1, kMaxElements);
    if (!acc.get("bufferView")) fail("accessors without a bufferView are not supported");
    const Json& bv = d.item("bufferViews", acc.get("bufferView")->integer("bufferView", 0, 1 << 30));
    if (d.buffers.empty()) fail("a bufferView with no buffers");
    const std::int64_t buf = need(bv, "buffer", 0, std::int64_t(d.buffers.size()) - 1);
    const std::int64_t bvOff = intOf(bv, "byteOffset", 0, 0, kMaxBytes), bvLen = need(bv, "byteLength", 1, kMaxBytes);
    const std::int64_t elem = std::int64_t(csize) * comps;
    const std::int64_t stride = intOf(bv, "byteStride", elem, 4, 252);
    if (stride < elem) fail("byteStride shorter than an element");
    const std::int64_t accOff = intOf(acc, "byteOffset", 0, 0, kMaxBytes);
    const auto& data = d.buffers[std::size_t(buf)];
    if (bvOff + bvLen > std::int64_t(data.size())) fail("bufferView runs past its buffer");
    if (accOff + (count - 1) * stride + elem > bvLen) fail("accessor runs past its bufferView");
    return {data.data() + bvOff + accOff, count, stride, comps, int(type)};
}

std::vector<float> floats(const View& v, int want){
    if (v.type != 5126 || v.comps != want) fail("expected a float accessor of " + std::to_string(want) + " components");
    std::vector<float> out(std::size_t(v.count) * want);
    for (std::int64_t i = 0; i < v.count; ++i) std::memcpy(&out[std::size_t(i) * want], v.base + i * v.stride, sizeof(float) * want);
    return out;
}
std::vector<std::uint32_t> indices(const View& v){
    if (v.comps != 1 || (v.type != 5121 && v.type != 5123 && v.type != 5125)) fail("indices must be unsigned scalars");
    std::vector<std::uint32_t> out(std::size_t(v.count));
    for (std::int64_t i = 0; i < v.count; ++i){
        const std::uint8_t* p = v.base + i * v.stride;
        out[std::size_t(i)] = v.type == 5121 ? p[0] : v.type == 5123 ? std::uint32_t(p[0] | p[1] << 8) : le32(p);
    }
    return out;
}

// Column-major 4x4 (glTF's order).
using M4 = std::array<double, 16>;
M4 mul(const M4& a, const M4& b){
    M4 r{};
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr){ double s = 0; for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
    return r;
}
M4 nodeMatrix(const Json& n){
    M4 m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto vec = [&](const char* key, std::size_t len, std::vector<double> def){
        const Json* v = n.get(key);
        if (!v) return def;
        if (!v->is(Json::Kind::Array) || v->a.size() != len) fail(std::string("node ") + key + " has the wrong length");
        std::vector<double> o(len);
        for (std::size_t i = 0; i < len; ++i) o[i] = v->a[i].number(key);
        return o;
    };
    if (n.get("matrix")){ const auto v = vec("matrix", 16, {}); for (int i = 0; i < 16; ++i) m[std::size_t(i)] = v[std::size_t(i)]; return m; }
    const auto t = vec("translation", 3, {0, 0, 0}), r = vec("rotation", 4, {0, 0, 0, 1}), s = vec("scale", 3, {1, 1, 1});
    const double x = r[0], y = r[1], z = r[2], w = r[3];
    const M4 R{1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y), 0, 2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x), 0,
               2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y), 0, 0, 0, 0, 1};
    M4 S{s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, 0, 0, 0, 1}, T{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1};
    return mul(T, mul(R, S));
}

void addPrimitive(const Doc& d, const Json& prim, const M4& world, const std::string& name, GltfModel& out){
    if (intOf(prim, "mode", 4, 0, 6) != 4){ ++out.skippedPrimitives; return; }
    const Json* attrs = prim.get("attributes");
    if (!attrs || !attrs->get("POSITION")) fail("primitive without POSITION");
    const auto pos = floats(accessor(d, attrs->get("POSITION")->integer("POSITION", 0, 1 << 30)), 3);
    const std::size_t nv = pos.size() / 3;
    std::vector<float> nrm;
    if (const Json* nn = attrs->get("NORMAL")){ nrm = floats(accessor(d, nn->integer("NORMAL", 0, 1 << 30)), 3); if (nrm.size() != pos.size()) fail("NORMAL count differs from POSITION"); }
    std::vector<std::uint32_t> idx;
    if (const Json* ii = prim.get("indices")) idx = indices(accessor(d, ii->integer("indices", 0, 1 << 30)));
    else { idx.resize(nv); for (std::size_t i = 0; i < nv; ++i) idx[i] = std::uint32_t(i); }
    if (idx.size() % 3) fail("triangle indices are not a multiple of three");
    for (const std::uint32_t i : idx) if (i >= nv) fail("index " + std::to_string(i) + " past the vertex count");
    GltfMesh m;
    m.name = name;
    if (const Json* mi = prim.get("material")){
        const Json& mat = d.item("materials", mi->integer("material", 0, 1 << 30));
        if (const Json* pbr = mat.get("pbrMetallicRoughness")) if (const Json* bc = pbr->get("baseColorFactor")){
            if (!bc->is(Json::Kind::Array) || bc->a.size() != 4) fail("baseColorFactor must have four values");
            m.baseColor = {float(bc->a[0].number("baseColorFactor")), float(bc->a[1].number("baseColorFactor")), float(bc->a[2].number("baseColorFactor"))};
        }
    }
    // Normals transform by the inverse transpose; for the cofactor matrix the scale does not matter.
    const M4& W = world;
    const double cof[9] = {W[5] * W[10] - W[9] * W[6], W[9] * W[2] - W[1] * W[10], W[1] * W[6] - W[5] * W[2],
                           W[8] * W[6] - W[4] * W[10], W[0] * W[10] - W[8] * W[2], W[4] * W[2] - W[0] * W[6],
                           W[4] * W[9] - W[8] * W[5], W[8] * W[1] - W[0] * W[9], W[0] * W[5] - W[4] * W[1]};
    m.positions.resize(nv); m.normals.resize(nv);
    for (std::size_t i = 0; i < nv; ++i){
        const double x = pos[3 * i], y = pos[3 * i + 1], z = pos[3 * i + 2];
        m.positions[i] = {float(W[0] * x + W[4] * y + W[8] * z + W[12]), float(W[1] * x + W[5] * y + W[9] * z + W[13]), float(W[2] * x + W[6] * y + W[10] * z + W[14])};
    }
    if (!nrm.empty()) for (std::size_t i = 0; i < nv; ++i){
        const double x = nrm[3 * i], y = nrm[3 * i + 1], z = nrm[3 * i + 2];
        double v[3] = {cof[0] * x + cof[3] * y + cof[6] * z, cof[1] * x + cof[4] * y + cof[7] * z, cof[2] * x + cof[5] * y + cof[8] * z};
        const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        m.normals[i] = l > 0 ? Vec3{float(v[0] / l), float(v[1] / l), float(v[2] / l)} : Vec3{0, 1, 0};
    } else {
        // No normals: area-weighted vertex normals from the triangles.
        std::vector<double> acc(nv * 3, 0.0);
        for (std::size_t t = 0; t < idx.size(); t += 3){
            const Vec3 a = m.positions[idx[t]], b = m.positions[idx[t + 1]], c = m.positions[idx[t + 2]];
            const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            const double n[3] = {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
            for (int k = 0; k < 3; ++k) for (int q = 0; q < 3; ++q) acc[3 * idx[t + k] + q] += n[q];
        }
        for (std::size_t i = 0; i < nv; ++i){
            const double l = std::sqrt(acc[3 * i] * acc[3 * i] + acc[3 * i + 1] * acc[3 * i + 1] + acc[3 * i + 2] * acc[3 * i + 2]);
            m.normals[i] = l > 0 ? Vec3{float(acc[3 * i] / l), float(acc[3 * i + 1] / l), float(acc[3 * i + 2] / l)} : Vec3{0, 1, 0};
        }
    }
    for (const Vec3& p : m.positions) if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) fail("a position is not finite");
    m.indices = std::move(idx);
    out.meshes.push_back(std::move(m));
}

void visit(const Doc& d, std::int64_t node, const M4& parent, std::unordered_set<std::int64_t>& path, int depth, GltfModel& out){
    if (depth > kMaxNodeDepth) fail("node tree too deep");
    if (!path.insert(node).second) fail("node tree has a cycle");
    const Json& n = d.item("nodes", node);
    const M4 world = mul(parent, nodeMatrix(n));
    if (const Json* mi = n.get("mesh")){
        const Json& mesh = d.item("meshes", mi->integer("mesh", 0, 1 << 30));
        const Json* prims = mesh.get("primitives");
        if (!prims || !prims->is(Json::Kind::Array)) fail("mesh without primitives");
        const Json* nm = mesh.get("name");
        for (const Json& p : prims->a){
            if (!p.is(Json::Kind::Object)) fail("primitive is not an object");
            addPrimitive(d, p, world, nm && nm->is(Json::Kind::String) ? nm->s : "", out);
        }
    }
    if (const Json* ch = n.get("children")){
        if (!ch->is(Json::Kind::Array)) fail("children is not an array");
        for (const Json& c : ch->a) visit(d, c.integer("child", 0, 1 << 30), world, path, depth + 1, out);
    }
    path.erase(node);
}
}

GltfModel loadGltf(std::span<const std::uint8_t> file, const UriReader& read){
    std::string_view text;
    std::span<const std::uint8_t> bin;
    if (file.size() >= 12 && le32(file.data()) == 0x46546C67u){         // "glTF": a GLB container
        if (le32(file.data() + 4) != 2) fail("GLB version is not 2");
        const std::size_t total = le32(file.data() + 8);
        if (total > file.size() || total < 20) fail("GLB length does not match the file");
        std::size_t at = 12;
        while (at + 8 <= total){
            const std::size_t len = le32(file.data() + at), type = le32(file.data() + at + 4);
            if (len > total - at - 8) fail("GLB chunk runs past the file");
            if (type == 0x4E4F534Au && text.empty()) text = {reinterpret_cast<const char*>(file.data() + at + 8), len};
            else if (type == 0x004E4942u && bin.empty()) bin = file.subspan(at + 8, len);
            at += 8 + ((len + 3) & ~std::size_t(3));
        }
        if (text.empty()) fail("GLB without a JSON chunk");
    } else text = {reinterpret_cast<const char*>(file.data()), file.size()};
    const Json j = parseJson(text);
    if (!j.is(Json::Kind::Object)) fail("the document is not an object");
    const Json* asset = j.get("asset");
    if (!asset || !asset->get("version") || !asset->get("version")->is(Json::Kind::String) || asset->get("version")->s.rfind("2.", 0) != 0) fail("asset.version must be 2.x");
    Doc d{j, {}};
    for (const Json& b : d.arr("buffers").a){
        if (!b.is(Json::Kind::Object)) fail("buffer is not an object");
        const std::int64_t len = need(b, "byteLength", 1, kMaxBytes);
        std::vector<std::uint8_t> data;
        const Json* uri = b.get("uri");
        if (!uri){ if (bin.empty()) fail("a buffer without a uri needs a GLB binary chunk"); data.assign(bin.begin(), bin.end()); }
        else {
            if (!uri->is(Json::Kind::String)) fail("buffer uri is not a string");
            const std::string& u = uri->s;
            const std::size_t comma = u.find(',');
            if (u.rfind("data:", 0) == 0){
                if (comma == std::string::npos || comma < 12 || u.compare(comma - 7, 7, ";base64") != 0) fail("data uri must be base64");
                data = base64(std::string_view(u).substr(comma + 1));
            } else if (!read || !read(u, data)) fail("cannot read buffer " + u.substr(0, 64));
        }
        if (std::int64_t(data.size()) < len) fail("buffer shorter than its byteLength");
        data.resize(std::size_t(len));
        d.buffers.push_back(std::move(data));
    }
    GltfModel out;
    const std::int64_t scene = intOf(j, "scene", 0, 0, 1 << 30);
    const M4 I{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::unordered_set<std::int64_t> path;
    if (!d.arr("scenes").a.empty()){
        const Json& s = d.item("scenes", scene);
        if (const Json* nodes = s.get("nodes")){
            if (!nodes->is(Json::Kind::Array)) fail("scene nodes is not an array");
            for (const Json& n : nodes->a) visit(d, n.integer("scene node", 0, 1 << 30), I, path, 0, out);
        }
    } else for (std::size_t i = 0; i < d.arr("nodes").a.size(); ++i) visit(d, std::int64_t(i), I, path, 0, out);
    bool first = true;
    for (const GltfMesh& m : out.meshes) for (const Vec3& p : m.positions){
        if (first){ out.boundsMin = out.boundsMax = p; first = false; }
        out.boundsMin = {std::min(out.boundsMin.x, p.x), std::min(out.boundsMin.y, p.y), std::min(out.boundsMin.z, p.z)};
        out.boundsMax = {std::max(out.boundsMax.x, p.x), std::max(out.boundsMax.y, p.y), std::max(out.boundsMax.z, p.z)};
    }
    return out;
}

void addToScene(const GltfModel& model, Scene& scene){
    for (const GltfMesh& g : model.meshes){
        Mesh m(scene.meshes.get_allocator().arena_);
        m.positions.assign(g.positions.begin(), g.positions.end());
        m.normals.assign(g.normals.begin(), g.normals.end());
        for (const std::uint32_t i : g.indices) m.indices.push_back(int(i));
        m.material.albedo = g.baseColor;
        scene.meshes.push_back(std::move(m));
    }
}

}

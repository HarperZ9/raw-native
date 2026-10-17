#include "raw/renderer/accel.hpp"
#include <cstdlib>
#include <cstring>
namespace raw {
namespace {
LinearAccel::Mode modeFromEnv(){
    const char* v = std::getenv("RAW_NATIVE_AO_ORACLE");
    if (v && std::strcmp(v, "scan") == 0) return LinearAccel::Mode::Scan;
    if (v && std::strcmp(v, "skew") == 0) return LinearAccel::Mode::Skew;
    return LinearAccel::Mode::Bvh;
}
}
void LinearAccel::build(const Scene& s, Arena* arena){
    tris = std::vector<Tri, ArenaAllocator<Tri>>(ArenaAllocator<Tri>(arena));
    std::size_t count = 0;
    for (const Mesh& m : s.meshes) count += m.indices.size() / 3;
    tris.reserve(count);
    for (const Mesh& m : s.meshes)
        for (size_t i=0;i+2<m.indices.size();i+=3)
            tris.push_back(Tri{ m.positions[m.indices[i]],
                                m.positions[m.indices[i+1]],
                                m.positions[m.indices[i+2]] });
    mode = modeFromEnv();
    if (mode != Mode::Scan) bvh.build(std::vector<Tri>(tris.begin(), tris.end()));
}
bool LinearAccel::occluded(const Ray& r, float maxDist) const {
    if (mode == Mode::Scan) return occludedScan(r, maxDist);
    return bvh.occluded(r, mode == Mode::Skew ? maxDist * 0.99f : maxDist);
}
bool LinearAccel::occludedScan(const Ray& r, float maxDist) const {
    float t,u,v;
    for (const Tri& tri : tris)
        if (intersectTri(r, tri, t,u,v) && t < maxDist) return true;
    return false;
}
}

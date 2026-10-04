#include "raw/tools/cli_params.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <cstdio>
namespace raw {
Camera cameraFromParams(const CliParams& p){
    Camera c;
    c.eye = p.eye; c.center = p.center; c.up = p.up;
    c.fovy = p.fovy;
    c.aspect = p.height > 0 ? (float)p.width / (float)p.height : 1.0f;
    return c;
}
Camera prevCameraFromParams(const CliParams& p){
    Camera c = cameraFromParams(p);
    if (p.prevEye)    c.eye    = *p.prevEye;
    if (p.prevCenter) c.center = *p.prevCenter;
    if (p.prevUp)     c.up     = *p.prevUp;
    return c;
}

// --- tiny stdlib helpers (no third-party JSON) ---------------------------

// Parse "x,y,z" (whitespace tolerant) into a Vec3. Returns false on a malformed
// triple so the caller can refuse rather than invent a value.
static bool parseVec3(const std::string& s, Vec3& out){
    float v[3]; int n = 0;
    const char* p = s.c_str();
    while (n < 3){
        char* end = nullptr;
        double d = std::strtod(p, &end);
        if (end == p) return false;            // no number consumed
        v[n++] = (float)d;
        p = end;
        while (*p == ' ' || *p == '\t') ++p;
        if (n < 3){ if (*p != ',') return false; ++p; }
    }
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '\0') return false;              // trailing garbage
    out = {v[0], v[1], v[2]};
    return true;
}

static bool parseFloat(const std::string& s, float& out){
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    out = (float)d; return true;
}
static bool parseInt(const std::string& s, int& out){
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = (int)v; return true;
}

std::optional<CliParams> parseArgs(int argc, const char* const* argv, std::string& err){
    CliParams p;
    // Back-compat: a lone leading positional arg (not a flag) is the out dir.
    int i = 1;
    if (argc > 1 && argv[1][0] != '-'){ p.out = argv[1]; i = 2; }
    // --params is applied first so explicit flags can override the file.
    for (int j = i; j + 1 < argc; ++j){
        if (std::strcmp(argv[j], "--params") == 0){
            if (!loadParamsFile(argv[j+1], p, err)) return std::nullopt;
        }
    }
    auto need = [&](int& k)->const char*{
        if (k + 1 >= argc){ err = std::string("missing value for ") + argv[k]; return nullptr; }
        return argv[++k];
    };
    for (; i < argc; ++i){
        std::string a = argv[i];
        if (a == "--params"){ if (!need(i)) return std::nullopt; continue; } // already applied
        else if (a == "--out"){ const char* v = need(i); if (!v) return std::nullopt; p.out = v; }
        else if (a == "--width"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseInt(v, p.width)){ err = "bad --width"; return std::nullopt; } }
        else if (a == "--height"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseInt(v, p.height)){ err = "bad --height"; return std::nullopt; } }
        else if (a == "--fovy"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseFloat(v, p.fovy)){ err = "bad --fovy"; return std::nullopt; } }
        else if (a == "--tolerance"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseFloat(v, p.tolerance) || !(p.tolerance >= 0.0f)){ err = "bad --tolerance"; return std::nullopt; } }
        else if (a == "--threads"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseInt(v, p.threads) || p.threads < 1 || p.threads > 256){ err = "bad --threads (1..256)"; return std::nullopt; } }
        else if (a == "--bench"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseInt(v, p.bench) || p.bench < 1 || p.bench > 1000){ err = "bad --bench (1..1000)"; return std::nullopt; } }
        else if (a == "--no-rt"){ p.rtao = false; }
        else if (a == "--gpu"){ p.gpu = true; }
        else if (a == "--eye"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseVec3(v, p.eye)){ err = "bad --eye (want x,y,z)"; return std::nullopt; } }
        else if (a == "--target"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseVec3(v, p.center)){ err = "bad --target (want x,y,z)"; return std::nullopt; } }
        else if (a == "--up"){ const char* v = need(i); if (!v) return std::nullopt;
            if (!parseVec3(v, p.up)){ err = "bad --up (want x,y,z)"; return std::nullopt; } }
        else if (a == "--prev-eye"){ const char* v = need(i); if (!v) return std::nullopt;
            Vec3 t; if (!parseVec3(v, t)){ err = "bad --prev-eye"; return std::nullopt; } p.prevEye = t; }
        else if (a == "--prev-target"){ const char* v = need(i); if (!v) return std::nullopt;
            Vec3 t; if (!parseVec3(v, t)){ err = "bad --prev-target"; return std::nullopt; } p.prevCenter = t; }
        else if (a == "--prev-up"){ const char* v = need(i); if (!v) return std::nullopt;
            Vec3 t; if (!parseVec3(v, t)){ err = "bad --prev-up"; return std::nullopt; } p.prevUp = t; }
        else { err = "unknown flag: " + a; return std::nullopt; }
    }
    if (p.width <= 0 || p.height <= 0){ err = "width/height must be positive"; return std::nullopt; }
    return p;
}

// --- minimal flat-JSON reader -------------------------------------------
// Finds a top-level "key": and returns the raw value text after the colon.
// Sufficient for our small flat params object; not a general JSON parser.

static bool findValue(const std::string& j, const std::string& key, std::string& out){
    std::string needle = "\"" + key + "\"";
    size_t k = j.find(needle);
    if (k == std::string::npos) return false;
    size_t colon = j.find(':', k + needle.size());
    if (colon == std::string::npos) return false;
    size_t s = colon + 1;
    while (s < j.size() && std::isspace((unsigned char)j[s])) ++s;
    if (s >= j.size()) return false;
    if (j[s] == '['){
        size_t e = j.find(']', s);
        if (e == std::string::npos) return false;
        out = j.substr(s, e - s + 1); return true;
    }
    if (j[s] == '"'){
        size_t e = j.find('"', s + 1);
        if (e == std::string::npos) return false;
        out = j.substr(s + 1, e - s - 1); return true;   // string contents (no quotes)
    }
    size_t e = s;
    while (e < j.size() && j[e] != ',' && j[e] != '}' && !std::isspace((unsigned char)j[e])) ++e;
    out = j.substr(s, e - s); return true;
}

// Parse a JSON array literal "[x,y,z]" into a Vec3.
static bool jsonArrayVec3(const std::string& arr, Vec3& out){
    std::string inner = arr;
    if (!inner.empty() && inner.front() == '[') inner.erase(inner.begin());
    if (!inner.empty() && inner.back()  == ']') inner.pop_back();
    return parseVec3(inner, out);
}

bool applyParamsJson(const std::string& json, CliParams& p, std::string& err){
    std::string v;
    if (findValue(json, "out", v)) p.out = v;
    if (findValue(json, "width", v)  && !parseInt(v, p.width)){ err = "bad json width"; return false; }
    if (findValue(json, "height", v) && !parseInt(v, p.height)){ err = "bad json height"; return false; }
    if (findValue(json, "fovy", v)   && !parseFloat(v, p.fovy)){ err = "bad json fovy"; return false; }
    if (findValue(json, "tolerance", v) && (!parseFloat(v, p.tolerance) || !(p.tolerance >= 0.0f))){
        err = "bad json tolerance"; return false; }
    if (findValue(json, "rt", v)){
        if (v == "true") p.rtao = true; else if (v == "false") p.rtao = false;
        else { err = "bad json rt"; return false; } }
    Vec3 t;
    if (findValue(json, "eye", v)){ if (!jsonArrayVec3(v, t)){ err = "bad json eye"; return false; } p.eye = t; }
    if (findValue(json, "target", v)){ if (!jsonArrayVec3(v, t)){ err = "bad json target"; return false; } p.center = t; }
    if (findValue(json, "up", v)){ if (!jsonArrayVec3(v, t)){ err = "bad json up"; return false; } p.up = t; }
    if (findValue(json, "prev_eye", v) && v != "null"){ if (!jsonArrayVec3(v, t)){ err = "bad json prev_eye"; return false; } p.prevEye = t; }
    if (findValue(json, "prev_target", v) && v != "null"){ if (!jsonArrayVec3(v, t)){ err = "bad json prev_target"; return false; } p.prevCenter = t; }
    if (findValue(json, "prev_up", v) && v != "null"){ if (!jsonArrayVec3(v, t)){ err = "bad json prev_up"; return false; } p.prevUp = t; }
    if (p.width <= 0 || p.height <= 0){ err = "json width/height must be positive"; return false; }
    return true;
}

bool loadParamsFile(const std::string& path, CliParams& p, std::string& err){
    std::ifstream f(path, std::ios::binary);
    if (!f){ err = "cannot open params file: " + path; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    return applyParamsJson(ss.str(), p, err);
}
// --- canonical params ----------------------------------------------------
static std::string cf(float v){ char b[32]; std::snprintf(b, sizeof b, "%.9g", (double)v); return b; }
static std::string cv(const Vec3& v){ return "[" + cf(v.x) + "," + cf(v.y) + "," + cf(v.z) + "]"; }
static std::string cov(const std::optional<Vec3>& v){ return v ? cv(*v) : std::string("null"); }
std::string canonicalParamsJson(const CliParams& p){
    std::string o = "{";
    if (p.gpu) o += "\"backend\":\"" + (p.gpuBackend.empty() ? std::string("none") : p.gpuBackend) + "\",";
    o += "\"eye\":" + cv(p.eye);
    o += ",\"fovy\":" + cf(p.fovy);
    o += ",\"height\":" + std::to_string(p.height);
    o += ",\"prev_eye\":" + cov(p.prevEye);
    o += ",\"prev_target\":" + cov(p.prevCenter);
    o += ",\"prev_up\":" + cov(p.prevUp);
    o += std::string(",\"rt\":") + (p.rtao ? "true" : "false");
    o += ",\"target\":" + cv(p.center);
    o += ",\"tolerance\":" + cf(p.tolerance);
    o += ",\"up\":" + cv(p.up);
    o += ",\"width\":" + std::to_string(p.width);
    o += "}";
    return o;
}
}

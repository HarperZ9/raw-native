#include "raw/tools/colour_cmd.hpp"
#include "raw/renderer/colour.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
namespace {
const char* kColourUsage =
    "usage: raw_native_cli colour list | grid OUT | apply PIPELINE IN OUT | tables PIPELINE OUT.json\n"
    "                         | encode pq|pq-decode|srgb-extended IN OUT\n"
    "  apply: float32 little-endian RGB triples in and out; pipelines are <tone>/<output>\n"
    "  encode: float64 little-endian scalars in and out, through the encoding alone (M1 criterion 3)\n";

bool writeFloats(const std::string& path, const std::vector<colour::RGB>& v){
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(v.data()), (std::streamsize)(v.size() * sizeof(colour::RGB)));
    return (bool)f;
}
bool readFloats(const std::string& path, std::vector<colour::RGB>& v){
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    if (n % (std::streamsize)sizeof(colour::RGB) != 0) return false;
    v.resize((size_t)n / sizeof(colour::RGB));
    f.seekg(0);
    return (bool)f.read(reinterpret_cast<char*>(v.data()), n);
}
bool readDoubles(const std::string& path, std::vector<double>& v){
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    if (n % (std::streamsize)sizeof(double) != 0) return false;
    v.resize((size_t)n / sizeof(double));
    f.seekg(0);
    return (bool)f.read(reinterpret_cast<char*>(v.data()), n);
}
bool writeDoubles(const std::string& path, const std::vector<double>& v){
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(v.data()), (std::streamsize)(v.size() * sizeof(double)));
    return (bool)f;
}
int fail(const char* what, const std::string& arg){ std::fprintf(stderr, "colour: %s: %s\n", what, arg.c_str()); return 2; }
}

int colourCommand(int argc, char** argv){
    const std::string sub = argc >= 2 ? argv[1] : "";
    if (sub == "list" && argc == 2){
        for (const std::string& n : colour::pipelineNames()) std::printf("%s\n", n.c_str());
        return 0;
    }
    if (sub == "grid" && argc == 3)
        return writeFloats(argv[2], colour::grid()) ? 0 : fail("cannot write", argv[2]);
    if (sub == "encode" && argc == 5){
        const std::string e = argv[2];
        double (*fn)(double) = e == "pq" ? colour::pqEncode : e == "pq-decode" ? colour::pqDecode
                             : e == "srgb-extended" ? colour::srgbEncodeExtended : nullptr;
        if (!fn) return fail("unknown encoding", e);
        std::vector<double> v;
        if (!readDoubles(argv[3], v)) return fail("cannot read float64 values from", argv[3]);
        for (double& x : v) x = fn(x);
        return writeDoubles(argv[4], v) ? 0 : fail("cannot write", argv[4]);
    }
    colour::Pipeline p;
    if ((sub == "apply" && argc == 5) || (sub == "tables" && argc == 4)){
        if (!colour::parsePipeline(argv[2], p)) return fail("unknown pipeline", argv[2]);
        const colour::Transform t(p);
        if (sub == "tables"){
            std::ofstream f(argv[3], std::ios::binary);
            f << t.aces2Json() << "\n";
            return f ? 0 : fail("cannot write", argv[3]);
        }
        std::vector<colour::RGB> v;
        if (!readFloats(argv[3], v)) return fail("cannot read float32 RGB from", argv[3]);
        for (colour::RGB& c : v) c = t.apply(c);
        return writeFloats(argv[4], v) ? 0 : fail("cannot write", argv[4]);
    }
    std::fputs(kColourUsage, stderr);
    return 2;
}
}

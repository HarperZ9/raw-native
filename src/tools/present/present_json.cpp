// JSON output of the presentation host: the run record the gate reads (tests/gpu/present_gate.py).
#include "present_host.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
namespace raw::present {
std::string jstr(const std::string& s){
    std::string o = "\"";
    for (char c : s){
        if (c == '"' || c == '\\'){ o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((unsigned char)c < 0x20){ char b[8]; std::snprintf(b, sizeof b, "\\u%04x", (unsigned)c); o += b; }
        else o += c;
    }
    return o + "\"";
}
std::string jnum(double v){
    if (!std::isfinite(v)) return "null";
    char b[40]; std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}
bool writeText(const std::string& path, const std::string& text){
    std::ofstream f(path, std::ios::binary);
    f << text;
    return bool(f);
}
std::string runJson(const RunOptions& o, const RunResult& r){
    std::string j = "{\n \"schema\": \"raw-native.present-run/1\",\n";
    j += " \"mode\": " + jstr(o.offscreen ? "offscreen" : "presented") + ",\n";
    j += " \"adapter\": " + jstr(r.adapter) + ",\n";
    j += " \"backend\": " + jstr(r.backend) + ", \"window_system\": " + jstr(r.windowSystem) + ",\n";
    j += " \"width\": " + std::to_string(o.width) + ", \"height\": " + std::to_string(o.height) + ",\n";
    j += " \"refresh_hz\": " + jnum(r.refreshHz) + ", \"refresh_source\": " + jstr(r.refreshSource) + ",\n";
    j += " \"sync_interval\": " + std::to_string(o.syncInterval) + ", \"tearing\": " + (r.tearing ? "true" : "false") + ",\n";
    j += " \"control_blit_offset_x\": " + std::to_string(o.control ? 1 : 0) + ",\n";
    j += " \"lifecycle\": " + std::string(o.schedule.on ? "true" : "false") + ",\n";
    j += " \"recoveries\": " + std::to_string(r.recoveries) + ", \"drawn_while_minimised\": " + std::to_string(r.drawnWhileMinimised)
       + ", \"zero_resizes\": " + std::to_string(r.zeroResizes) + ", \"size_mismatches\": " + std::to_string(r.sizeMismatches) + ",\n";
    j += " \"fullscreen_checked\": " + std::string(r.fullscreenChecked ? "true" : "false")
       + ", \"fullscreen_ok\": " + (r.fullscreenOk ? "true" : "false") + ",\n";
    j += " \"error\": " + jstr(r.error) + ",\n";
    j += " \"graph_first_frame\": " + jstr(r.graph) + ",\n";
    j += " \"events\": [";
    for (size_t i = 0; i < r.events.size(); ++i) j += (i ? ", " : "") + jstr(r.events[i]);
    j += "],\n \"frames\": [\n";
    for (size_t i = 0; i < r.frames.size(); ++i){
        const FrameRecord& f = r.frames[i];
        j += "  {\"i\": " + std::to_string(f.frame) + ", \"w\": " + std::to_string(f.width) + ", \"h\": " + std::to_string(f.height)
           + ", \"sha256\": " + jstr(f.hash) + "}" + (i + 1 < r.frames.size() ? ",\n" : "\n");
    }
    return j + " ]\n}\n";
}
}  // namespace raw::present

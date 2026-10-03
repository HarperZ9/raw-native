#include "raw/channels_json.hpp"
#include "raw/certificate.hpp"
#include "raw/composite.hpp"
#include <sstream>
#include <cstdio>
#include <cmath>
#include <optional>
#include <algorithm>
namespace raw {
// Emit a finite double as a compact JSON number, or literal null when absent or
// non-finite. Identical honesty rule to certificate.cpp's jnum.
static std::string jnum(const std::optional<double>& v){
    if (!v.has_value() || !std::isfinite(*v)) return "null";
    char b[64]; std::snprintf(b, sizeof b, "%.6g", *v); return b;
}
static std::string jnum(double v){ return jnum(std::optional<double>(v)); }
static std::string jvec3(Vec3 v){
    std::ostringstream o;
    o << "[" << jnum(v.x) << "," << jnum(v.y) << "," << jnum(v.z) << "]";
    return o.str();
}

// Per-channel summaries computed over covered (masked) pixels only. Background
// is excluded so a statistic is never diluted by empty space.
struct Summaries {
    int covered{0}, total{0};
    std::optional<double> dmin, dmax, dmean;   // depth, covered only
    std::optional<Vec3>   nmean;               // mean covered normal
    double motionMax{0}, motionMean{0};        // covered motion magnitudes
    double clipFrac{0};                        // fraction of covered px with any HDR channel > 1
};

static Summaries summarize(const FrameResult& o){
    Summaries s;
    const GBuffer& g = o.g;
    s.total = g.w * g.h;
    double dsum = 0, msum = 0; Vec3 nsum{0,0,0};
    int clip = 0;
    bool anyDepth = false;
    for (int y = 0; y < g.h; ++y) for (int x = 0; x < g.w; ++x){
        if (!g.mask.at(x,y)) continue;
        ++s.covered;
        float d = g.depth.at(x,y);
        if (std::isfinite(d)){
            if (!anyDepth){ s.dmin = d; s.dmax = d; anyDepth = true; }
            else { s.dmin = std::min(*s.dmin, (double)d); s.dmax = std::max(*s.dmax, (double)d); }
            dsum += d;
        }
        Vec3 n = g.normal.at(x,y); nsum = nsum + n;
        float mag = length(g.motion.at(x,y));
        s.motionMax = std::max(s.motionMax, (double)mag); msum += mag;
        Vec3 h = o.hdr.at(x,y);
        if (h.x > 1.0f || h.y > 1.0f || h.z > 1.0f) ++clip;
    }
    if (s.covered > 0){
        if (anyDepth) s.dmean = dsum / s.covered;
        s.nmean = normalize(nsum * (1.0f / (float)s.covered));
        s.motionMean = msum / s.covered;
        s.clipFrac = (double)clip / s.covered;
    }
    return s;
}

// A coarse downsampled luminance readout (0..1) of the displayable frame, so a
// model gets a glanceable picture without decoding the PPM. Box-average of the
// tonemapped/clamped frame's Rec.709 luma over each cell.
static std::string readoutJson(const Buffer<Vec3>& frame, int rw, int rh){
    rw = std::max(1, rw); rh = std::max(1, rh);
    std::ostringstream o;
    o << "{\"kind\":\"luminance\",\"width\":" << rw << ",\"height\":" << rh << ",\"rows\":[";
    for (int cy = 0; cy < rh; ++cy){
        if (cy) o << ",";
        o << "[";
        for (int cx = 0; cx < rw; ++cx){
            if (cx) o << ",";
            int x0 = (int)((long long)cx * frame.w / rw), x1 = (int)((long long)(cx+1) * frame.w / rw);
            int y0 = (int)((long long)cy * frame.h / rh), y1 = (int)((long long)(cy+1) * frame.h / rh);
            x1 = std::max(x1, x0 + 1); y1 = std::max(y1, y0 + 1);
            x1 = std::min(x1, frame.w); y1 = std::min(y1, frame.h);
            double sum = 0; int n = 0;
            for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x){
                Vec3 c = frame.at(x,y);
                sum += 0.2126*c.x + 0.7152*c.y + 0.0722*c.z; ++n;
            }
            o << jnum(n ? sum / n : 0.0);
        }
        o << "]";
    }
    o << "]}";
    return o.str();
}

std::string channelsJson(const FrameResult& o, const CliParams& p, int rw, int rh){
    Summaries s = summarize(o);

    // The honest fidelity witnesses, exactly as the CLI prints them: motion
    // coherence is null when no covered pixel had a denominator; HDR headroom is
    // the real max radiance.
    std::optional<double> motionCoherence =
        o.motionTotal > 0 ? std::optional<double>((double)o.motionValid / o.motionTotal)
                          : std::nullopt;
    std::optional<double> hdrHeadroom = std::optional<double>(maxRadiance(o.hdr));
    Certificate cert = certificate_with_channels(o.rec, p.tolerance, motionCoherence, hdrHeadroom);

    std::ostringstream j;
    j << "{";
    j << "\"schema\":\"raw-channels/1\",";
    // frame block
    j << "\"frame\":{\"width\":" << o.g.w << ",\"height\":" << o.g.h
      << ",\"files\":{\"ppm\":\"frame.ppm\",\"pfm\":\"frame_hdr.pfm\","
      << "\"ao_rt\":\"ao_rt.pgm\",\"ao_ss\":\"ao_ss.pgm\",\"ao_error\":\"ao_error.pgm\","
      << "\"ao_rt_exact\":\"ao_rt.pfm\",\"ao_ss_exact\":\"ao_ss.pfm\",\"mask\":\"mask.pgm\"}},";
    // camera block (the view that produced the channels; closes the loop)
    Camera cam = cameraFromParams(p);
    j << "\"camera\":{\"eye\":" << jvec3(cam.eye) << ",\"target\":" << jvec3(cam.center)
      << ",\"up\":" << jvec3(cam.up) << ",\"fovy\":" << jnum(cam.fovy)
      << ",\"aspect\":" << jnum(cam.aspect) << ",\"prev\":";
    if (p.hasPrevCamera()){
        Camera pc = prevCameraFromParams(p);
        j << "{\"eye\":" << jvec3(pc.eye) << ",\"target\":" << jvec3(pc.center)
          << ",\"up\":" << jvec3(pc.up) << "}";
    } else j << "null";
    j << "},";
    // the witnessed certificate verbatim
    j << "\"certificate\":" << to_json(cert) << ",";
    // compact per-channel summaries
    j << "\"channels\":{";
    j << "\"coverage\":" << jnum(s.total > 0 ? (double)s.covered / s.total : 0.0) << ",";
    // depth
    if (s.dmean.has_value())
        j << "\"depth\":{\"min\":" << jnum(s.dmin) << ",\"max\":" << jnum(s.dmax)
          << ",\"mean\":" << jnum(s.dmean) << "},";
    else j << "\"depth\":null,";
    // normal
    if (s.nmean.has_value()) j << "\"normal\":{\"mean\":" << jvec3(*s.nmean) << "},";
    else j << "\"normal\":null,";
    // motion (always present: counts are honest, coherence may be null)
    j << "\"motion\":{\"valid\":" << o.motionValid << ",\"total\":" << o.motionTotal
      << ",\"coherence\":" << jnum(motionCoherence)
      << ",\"max_magnitude\":" << jnum(s.motionMax)
      << ",\"mean_magnitude\":" << jnum(s.motionMean) << "},";
    // hdr
    j << "\"hdr\":{\"headroom\":" << jnum(hdrHeadroom)
      << ",\"clipping_fraction\":" << jnum(s.clipFrac) << "},";
    // ao
    std::optional<double> aoFid = o.rec.pixels > 0
        ? std::optional<double>(aoFidelityFromRmse(o.rec.rmse)) : std::nullopt;
    j << "\"ao\":{\"rmse\":" << jnum(o.rec.rmse) << ",\"max_error\":" << jnum(o.rec.maxError)
      << ",\"fidelity\":" << jnum(aoFid)
      << ",\"within_tolerance\":" << (o.rec.withinTolerance ? "true" : "false") << "},";
    // coarse readout
    j << "\"readout\":" << readoutJson(o.frame, rw, rh);
    j << "}";   // channels
    j << "}";
    return j.str();
}
}

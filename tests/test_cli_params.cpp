// CLI render-parameter parsing + the two-way contract: params map to the right
// camera, a JSON params file applies, and two different cameras produce two
// different renders (so a model's steer actually changes what it perceives).
#include "raw/cli_params.hpp"
#include "raw/render.hpp"
#include "raw/raster.hpp"
#include "raw/composite.hpp"
#include "check.hpp"
#include <string>
#include <vector>
#include <optional>
#include <cmath>
using namespace raw;

static Scene sceneFor(const CliParams& p){
    Scene s = buildTestScene(p.width, p.height);
    s.camera = cameraFromParams(p);
    return s;
}
static FrameResult renderFor(const CliParams& p){
    Scene s = sceneFor(p);
    Mat4 curVP  = mul(s.camera.proj(), s.camera.view());
    Mat4 prevVP = curVP;
    if (p.hasPrevCamera()){
        Camera pc = prevCameraFromParams(p);
        prevVP = mul(pc.proj(), pc.view());
    }
    return renderWithParams(s, p.width, p.height, prevVP);
}

int main(){
    // (1) argv flags map onto the right CliParams + Camera.
    const char* argv[] = {"cli", "--out", "myout", "--width", "80", "--height", "40",
                          "--eye", "1,2,3", "--target", "0,0.5,0", "--up", "0,1,0",
                          "--fovy", "0.75"};
    std::string err;
    auto p = parseArgs((int)(sizeof(argv)/sizeof(argv[0])), argv, err);
    CHECK(p.has_value());
    if (p){
        CHECK(p->out == "myout");
        CHECK(p->width == 80 && p->height == 40);
        CHECK_NEAR(p->eye.x, 1.0, 1e-6); CHECK_NEAR(p->eye.y, 2.0, 1e-6); CHECK_NEAR(p->eye.z, 3.0, 1e-6);
        CHECK_NEAR(p->center.y, 0.5, 1e-6);
        CHECK_NEAR(p->fovy, 0.75, 1e-6);
        Camera c = cameraFromParams(*p);
        CHECK_NEAR(c.eye.x, 1.0, 1e-6);
        CHECK_NEAR(c.aspect, 80.0/40.0, 1e-6);   // aspect derived from width/height
        CHECK_NEAR(c.fovy, 0.75, 1e-6);
    }

    // (2) back-compat: a lone positional arg is still the out dir.
    const char* argv2[] = {"cli", "somedir"};
    auto p2 = parseArgs(2, argv2, err);
    CHECK(p2.has_value());
    if (p2){ CHECK(p2->out == "somedir"); CHECK(p2->width == 256 && p2->height == 256); }

    // (3) a malformed vector is refused, not silently coerced.
    const char* argv3[] = {"cli", "--eye", "1,2"};   // only two components
    auto p3 = parseArgs(3, argv3, err);
    CHECK(!p3.has_value());

    // (4) a JSON params object applies onto the params (stdlib parse, no dep).
    CliParams pj;
    std::string js = "{\"width\":120,\"height\":90,\"eye\":[2.0,3.0,4.0],"
                     "\"target\":[0,1,0],\"fovy\":1.1,\"prev_eye\":[2.5,3.0,4.0]}";
    CHECK(applyParamsJson(js, pj, err));
    CHECK(pj.width == 120 && pj.height == 90);
    CHECK_NEAR(pj.eye.x, 2.0, 1e-6); CHECK_NEAR(pj.eye.z, 4.0, 1e-6);
    CHECK_NEAR(pj.fovy, 1.1, 1e-6);
    CHECK(pj.prevEye.has_value());
    if (pj.prevEye) CHECK_NEAR(pj.prevEye->x, 2.5, 1e-6);

    // (5) THE TWO-WAY CONTRACT: two different cameras produce different output.
    // Render a small frame from two distinct eyes and confirm the displayable
    // frames differ (a steer the model makes actually changes what it perceives).
    CliParams a; a.width = 48; a.height = 48; a.eye = {4,4,6};  a.center = {0,1,0};
    CliParams b = a;            b.eye = {-4,4,6};               // mirrored across x
    FrameResult ra = renderFor(a);
    FrameResult rb = renderFor(b);
    CHECK(ra.frame.w == 48 && rb.frame.w == 48);
    int diff = 0;
    for (int i = 0; i < (int)ra.frame.px.size(); ++i){
        Vec3 da = ra.frame.px[i], db = rb.frame.px[i];
        if (std::fabs(da.x-db.x) > 1e-4f || std::fabs(da.y-db.y) > 1e-4f || std::fabs(da.z-db.z) > 1e-4f)
            ++diff;
    }
    CHECK(diff > 50);   // the two views are materially different, not a no-op

    // (6) a different FOV also changes the render (zoom is a real steer).
    CliParams z = a; z.fovy = a.fovy * 0.5f;   // narrower FOV
    FrameResult rz = renderFor(z);
    int diffZ = 0;
    for (int i = 0; i < (int)ra.frame.px.size(); ++i){
        Vec3 da = ra.frame.px[i], dz = rz.frame.px[i];
        if (std::fabs(da.x-dz.x) > 1e-4f || std::fabs(da.y-dz.y) > 1e-4f || std::fabs(da.z-dz.z) > 1e-4f)
            ++diffZ;
    }
    CHECK(diffZ > 50);

    return raw_test_summary();
}

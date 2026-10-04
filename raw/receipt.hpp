#pragma once
// superstack.receipt/1: the shared receipt of the superstack contract
// (third_party/superstack, MIT), written beside raw-cert/2. raw-cert/2 is
// unchanged; the receipt restates the same check in the contract's form:
// canonical JSON v2, a seal over every field, and two verdicts reported
// separately: identity (MATCH when the bytes equal the reference's, DRIFT
// otherwise) and tolerance (verified, refuted or unverifiable with a reason).
//
// receipt.json, beside every certificate.json:
//   subject    the screen-space AO buffer as float32 (content_sha256)
//   reference  the ray-traced AO buffer as float32
//   tolerance  the AO verdict of raw-cert/2, with the same RMSE and bound
// gpu_receipt.json, beside gpu_certificate.json:
//   subject    the GPU frame as RGB8
//   reference  the CPU frame as RGB8
//   tolerance  the verdict of raw-gpu-cert/1, with every channel and bound
#include "raw/cli_params.hpp"
#include "raw/gpu_reconcile.hpp"
#include "raw/render.hpp"
#include "raw/run.hpp"
#include <string>
#include <vector>
namespace raw {
// False in a build whose standard library cannot compile superstack.hpp
// (see CMakeLists.txt); such a build writes no receipts.
bool receiptsCompiled();
// The built-in scene for these params as a superstack.scene/1 object, in
// canonical JSON. For the default params it equals the contract's reference
// scene (superstack examples/pixels/scene.json) byte for byte.
std::string sceneJson(const CliParams& p);
// receipt.json for a rendered frame. `backend` names the path that made it,
// for example "raw-native-cpu". `outputs` lists the files written beside it,
// each with its SHA-256.
std::string aoReceiptJson(const FrameResult& o, const CliParams& p, const std::string& backend,
                          const FileDigests& outputs);
// gpu_receipt.json. `gpu` is null when no GPU frame exists; `cpu` is null
// when the reference was not rendered. The tolerance verdict is the one
// raw-gpu-cert/1 reached; unverifiable carries r.reason.
std::string gpuReceiptJson(const FrameResult* gpu, const FrameResult* cpu, const GpuReconcile& r,
                           const CliParams& p, const FileDigests& outputs);
// Write receipt.json into p.out for a frame whose image files, certificate.json
// and channels.json are already written. Returns false when it cannot write.
bool writeAoReceipt(const FrameResult& o, const CliParams& p, const std::string& backend,
                    const FileDigests& imageOutputs);
// Error codes from the contract's verify_receipt for a receipt's text (empty:
// well formed, seal intact), plus "parse" when it is not JSON.
std::vector<std::string> verifyReceiptText(const std::string& json);
// Recheck <dir>/receipt.json against the files beside it: the seal, every
// listed digest, the subject and reference hashes recomputed from ao_ss.pfm and
// ao_rt.pfm, and the tolerance verdict against certificate.json's. Appends one
// line per check to `report`; returns the number of mismatches.
int checkReceiptDir(const std::string& dir, const std::string& certVerdict, std::string& report);
}

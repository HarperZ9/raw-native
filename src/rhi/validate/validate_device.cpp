// The validating device (raw/rhi/validate.hpp): object tracking, the list's lifetime, mapRead
// and swapchains. The command rules are in validate_commands.cpp.
#include "validate_internal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace raw::rhi {
bool ValidationLog::has(const char* rule) const {
    for (const ValidationReport& r : reports) if (r.rule == rule) return true;
    return false;
}
}  // namespace raw::rhi

namespace raw::rhi::validate {
const char* accessText(Access a){
    switch (a){
        case Access::Undefined: return "undefined"; case Access::Uniform: return "uniform";
        case Access::StorageRead: return "storage-read"; case Access::StorageWrite: return "storage-write";
        case Access::CopySrc: return "copy-src"; case Access::CopyDst: return "copy-dst";
    }
    return "?";
}
std::string bufferName(const BufState* b){ return b ? "buffer '" + b->label + "'" : "an unknown buffer"; }

void ValidatingDevice::report(const char* rule, const std::string& message){
    log_->reports.push_back({rule, message});
    if (log_->echo) std::fprintf(stderr, "rhi validation %s: %s\n", rule, message.c_str());
}

BufferHandle ValidatingDevice::createBuffer(const BufferDesc& desc, std::string& err){
    const BufferHandle h = inner_.createBuffer(desc, err);
    if (h.valid()) buffers_[key(h)] = {desc, desc.label ? desc.label : ""};
    return h;
}
void ValidatingDevice::destroyBuffer(BufferHandle b){
    if (!buffers_.erase(key(b))){ report("V01", "destroyBuffer of a handle that is not a live buffer"); return; }
    inner_.destroyBuffer(b);
}
PipelineHandle ValidatingDevice::createComputePipeline(const ComputePipelineDesc& desc, std::string& err){
    const PipelineHandle h = inner_.createComputePipeline(desc, err);
    if (h.valid()) pipes_[key(h)] = std::vector<Binding>(desc.layout.begin(), desc.layout.end());
    return h;
}
TextureHandle ValidatingDevice::createTexture(const TextureDesc& desc, std::string& err){
    const TextureHandle h = inner_.createTexture(desc, err);
    if (h.valid()) textures_[key(h)] = {desc};
    return h;
}
void ValidatingDevice::destroyTexture(TextureHandle t){
    if (!textures_.erase(key(t))){ report("V01", "destroyTexture of a handle that is not a live texture"); return; }
    inner_.destroyTexture(t);
}
SamplerHandle ValidatingDevice::createSampler(const SamplerDesc& desc, std::string& err){
    const SamplerHandle h = inner_.createSampler(desc, err);
    if (h.valid()) samplers_[key(h)] = true;
    return h;
}
RasterPipelineHandle ValidatingDevice::createRasterPipeline(const RasterPipelineDesc& desc, std::string& err){
    const RasterPipelineHandle h = inner_.createRasterPipeline(desc, err);
    if (!h.valid()) return h;
    RasterState r; r.layout.assign(desc.layout.begin(), desc.layout.end()); r.targetCount = desc.targetCount;
    for (uint32_t i = 0; i < kMaxColorTargets; ++i) r.targets[i] = desc.targets[i];
    rasters_[key(h)] = r;
    return h;
}

CommandList* ValidatingDevice::begin(std::string& err){
    if (listOpen){ report("V08", "begin while a list is already open"); err = "rhi validation V08: begin while a list is open"; return nullptr; }
    list_.inner = inner_.begin(err);
    if (!list_.inner) return nullptr;
    listOpen = true; list_.passOpen = false;
    reportsAtBegin = log_->reports.size();
    for (auto& [k, b] : buffers_){ b.state = Access::Undefined; b.dirty = b.lastWasCopy = b.writtenInList = false; }
    return &list_;
}
bool ValidatingDevice::submitAndWait(std::string& err){
    if (!listOpen){ report("V08", "submitAndWait with no list begun"); err = "rhi validation V08: submit with nothing begun"; return false; }
    if (list_.passOpen) report("V09", "submitAndWait inside an open render pass");
    listOpen = false;
    const bool ok = inner_.submitAndWait(err);
    for (auto& [k, b] : buffers_) b.writtenInList = false;
    const std::size_t n = log_->reports.size() - reportsAtBegin;
    if (n){
        const ValidationReport& f = log_->reports[reportsAtBegin];
        err = "rhi validation: " + std::to_string(n) + " report(s) in this submission; first " + f.rule + ": " + f.message;
        return false;
    }
    return ok;
}
const void* ValidatingDevice::mapRead(BufferHandle b, uint64_t size, std::string& err){
    BufState* s = buffer(b);
    const char* rule = nullptr; std::string why;
    if (!s){ rule = "V01"; why = "mapRead of a handle that is not a live buffer"; }
    else if (!has(s->desc.usage, BufferUsage::MapRead)){ rule = "V03"; why = "mapRead of " + bufferName(s) + " without MapRead usage"; }
    else if (size > s->desc.size){ rule = "V13"; why = "mapRead of " + std::to_string(size) + " bytes from " + bufferName(s) + " of " + std::to_string(s->desc.size); }
    else if (listOpen && s->writtenInList){ rule = "V13"; why = "mapRead of " + bufferName(s) + ", which the open list writes"; }
    if (rule){ report(rule, why); err = std::string("rhi validation ") + rule + ": " + why; return nullptr; }
    return inner_.mapRead(b, size, err);
}
void ValidatingDevice::unmap(BufferHandle b){
    if (!buffer(b)){ report("V01", "unmap of a handle that is not a live buffer"); return; }
    inner_.unmap(b);
}

namespace {
// The swapchain's backbuffers are textures the backend makes; the wrapper registers each one it
// hands out, and prepare() records on the backend's own list.
class ValidatingSwapchain final : public Swapchain {
public:
    ValidatingSwapchain(std::unique_ptr<Swapchain> s, ValidatingDevice& d, ValidatingList* l) : s_(std::move(s)), d_(d), l_(l) {}
    TextureHandle current() override {
        const TextureHandle t = s_->current();
        if (t.valid()) d_.adoptTexture(t, {s_->width(), s_->height(), s_->format(), TextureUsage::RenderTarget | TextureUsage::CopySrc, "backbuffer"});
        return t;
    }
    uint32_t width() const override { return s_->width(); }
    uint32_t height() const override { return s_->height(); }
    TextureFormat format() const override { return s_->format(); }
    bool colourSpaceApplied() const override { return s_->colourSpaceApplied(); }
    bool tearingAllowed() const override { return s_->tearingAllowed(); }
    void prepare(CommandList& cmd) override {
        if (&cmd == l_ && l_->inner) s_->prepare(*l_->inner); else s_->prepare(cmd);
    }
    PresentStatus present(uint32_t sync, std::string& err) override { return s_->present(sync, err); }
    bool resize(uint32_t w, uint32_t h, std::string& err) override { return s_->resize(w, h, err); }
    bool waitForFrame(uint32_t ms) override { return s_->waitForFrame(ms); }
    PresentStats stats() override { return s_->stats(); }
    double refreshHz() override { return s_->refreshHz(); }
private:
    std::unique_ptr<Swapchain> s_;
    ValidatingDevice& d_;
    ValidatingList* l_;
};
bool envOn(const char* name){ const char* v = std::getenv(name); return v && std::strcmp(v, "1") == 0; }
}  // namespace

std::unique_ptr<Swapchain> ValidatingDevice::createSwapchain(const SwapchainDesc& desc, std::string& err){
    auto s = inner_.createSwapchain(desc, err);
    if (!s) return nullptr;
    return std::make_unique<ValidatingSwapchain>(std::move(s), *this, &list_);
}
}  // namespace raw::rhi::validate

namespace raw::rhi {
std::unique_ptr<Device> makeValidatingDevice(Device& inner, std::shared_ptr<ValidationLog> log){
    return std::make_unique<validate::ValidatingDevice>(inner, std::move(log));
}
Device* validatedIfRequested(Device* inner){
    if (!inner || !validate::envOn("RAW_NATIVE_RHI_VALIDATE")) return inner;
    static Device* wrappedFor = nullptr;
    static std::unique_ptr<Device> wrapper;
    if (wrappedFor != inner){
        auto log = std::make_shared<ValidationLog>();
        log->echo = true;
        wrapper = makeValidatingDevice(*inner, log);
        wrappedFor = inner;
    }
    return wrapper.get();
}
}  // namespace raw::rhi

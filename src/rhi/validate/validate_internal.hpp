#pragma once
// Private to src/rhi/validate/: the validating device and command list (raw/rhi/validate.hpp).
#include "raw/rhi/validate.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace raw::rhi::validate {

using Key = std::pair<uint32_t, uint32_t>;   // a handle's index and generation
template<class H> Key key(H h){ return {h.index, h.gen}; }

struct BufState {
    BufferDesc desc;
    std::string label;
    Access state{Access::Undefined};   // since the last barrier in the open list
    bool dirty{false};                 // written by a command of the open list, no barrier since
    bool lastWasCopy{false};           // that write was a copy or upload
    bool writtenInList{false};         // any write in the open list (for mapRead)
};
struct TexState { TextureDesc desc; };
struct RasterState { std::vector<RasterBinding> layout; TextureFormat targets[kMaxColorTargets]; uint32_t targetCount{1}; };

class ValidatingDevice;
class ValidatingList final : public CommandList {
public:
    explicit ValidatingList(ValidatingDevice& d) : dev_(d) {}
    void barrier(std::span<const BufferBarrier> barriers) override;
    void upload(BufferHandle dst, uint64_t offset, const void* data, uint64_t size) override;
    void copyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size) override;
    void dispatch(PipelineHandle pipeline, std::span<const BufferHandle> binds, uint32_t x, uint32_t y, uint32_t z) override;
    void uploadTexture(TextureHandle dst, const void* rgba, uint32_t width, uint32_t height) override;
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst) override;
    void beginRenderPass(const RenderPassDesc& pass) override;
    void draw(RasterPipelineHandle pipeline, std::span<const RasterBind> binds, uint32_t vertexCount) override;
    void endRenderPass() override;
    CommandList* inner{nullptr};
    bool passOpen{false};
    RenderPassDesc pass;
private:
    bool open(const char* what);
    // A buffer use: V01 unknown, V03 usage, V07 state or hazard. Marks the use when it passes.
    bool use(BufferHandle b, Access need, BufferUsage usage, const char* what);
    void mark(BufferHandle b, Access a);
    ValidatingDevice& dev_;
};

class ValidatingDevice final : public Device {
public:
    ValidatingDevice(Device& inner, std::shared_ptr<ValidationLog> log) : inner_(inner), log_(std::move(log)), list_(*this) {}
    const char* backendName() const override { return inner_.backendName(); }
    const AdapterInfo& adapter() const override { return inner_.adapter(); }
    ShaderFormat shaderFormat() const override { return inner_.shaderFormat(); }
    BufferHandle createBuffer(const BufferDesc& desc, std::string& err) override;
    void destroyBuffer(BufferHandle buffer) override;
    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc, std::string& err) override;
    TextureHandle createTexture(const TextureDesc& desc, std::string& err) override;
    void destroyTexture(TextureHandle texture) override;
    SamplerHandle createSampler(const SamplerDesc& desc, std::string& err) override;
    RasterPipelineHandle createRasterPipeline(const RasterPipelineDesc& desc, std::string& err) override;
    CommandList* begin(std::string& err) override;
    bool submitAndWait(std::string& err) override;
    const void* mapRead(BufferHandle buffer, uint64_t size, std::string& err) override;
    void unmap(BufferHandle buffer) override;
    std::unique_ptr<Swapchain> createSwapchain(const SwapchainDesc& desc, std::string& err) override;
    bool lost() const override { return inner_.lost(); }
    bool loseForTest(std::string& err) override { return inner_.loseForTest(err); }

    void report(const char* rule, const std::string& message);
    BufState* buffer(BufferHandle b){ auto it = buffers_.find(key(b)); return it == buffers_.end() ? nullptr : &it->second; }
    TexState* texture(TextureHandle t){ auto it = textures_.find(key(t)); return it == textures_.end() ? nullptr : &it->second; }
    const std::vector<Binding>* pipeline(PipelineHandle p){ auto it = pipes_.find(key(p)); return it == pipes_.end() ? nullptr : &it->second; }
    const RasterState* raster(RasterPipelineHandle p){ auto it = rasters_.find(key(p)); return it == rasters_.end() ? nullptr : &it->second; }
    bool sampler(SamplerHandle s) const { return samplers_.count(key(s)) != 0; }
    void adoptTexture(TextureHandle t, const TextureDesc& d){ textures_[key(t)] = {d}; }
    bool listOpen{false};
    std::size_t reportsAtBegin{0};
private:
    Device& inner_;
    std::shared_ptr<ValidationLog> log_;
    ValidatingList list_;
    std::map<Key, BufState> buffers_;
    std::map<Key, TexState> textures_;
    std::map<Key, std::vector<Binding>> pipes_;
    std::map<Key, RasterState> rasters_;
    std::map<Key, bool> samplers_;
};

const char* accessText(Access a);
std::string bufferName(const BufState* b);

}  // namespace raw::rhi::validate

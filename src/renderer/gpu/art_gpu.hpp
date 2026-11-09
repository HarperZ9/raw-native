#pragma once
// Private to the art checks: a sequence of compute passes (art module or style module, binding
// layouts from the generated headers) on an RHI device, with every buffer's access state tracked so
// the same steps can be submitted again for timing. One run uploads the inputs, records the steps
// and reads back the outputs; time() submits the steps alone, warm, and takes the median of the
// submit-to-fence times (the dispatch timer's method, evidence/gpu-dispatch-timer-bounds.json).
#include "raw/rhi/rhi.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace raw::gpu_check::art_gpu {

class Runner {
public:
    explicit Runner(rhi::Device& d) : dev_(d) {}
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;
    // A storage buffer of `bytes`, zero-filled; returns its index.
    int buffer(std::uint64_t bytes, const char* label);
    // A buffer uploaded at the start of the first run: storage, or the uniform when `uniform`.
    int input(const void* data, std::uint64_t bytes, const char* label, bool uniform = false);
    template<class T> int input(const std::vector<T>& v, const char* label) { return input(v.data(), v.size() * sizeof(T), label); }
    template<class T> int uniform(const T& u, const char* label) { return input(&u, sizeof u, label, true); }
    // A 64-wide pass over `count` invocations (folded into rows of 32,768 groups), or an explicit
    // group count. False with error() when the build has no such pass or its layout disagrees.
    bool pass(const char* name, const std::vector<int>& binds, std::uint32_t count);
    bool passGroups(const char* name, const std::vector<int>& binds, std::uint32_t gx, std::uint32_t gy);
    // Uploads (first run only), every step, then copies each buffer of `reads` into dst.
    bool run(const std::vector<std::pair<int, std::vector<std::uint32_t>*>>& reads);
    // The steps [first, last) recorded `inner` times into one submission, submitted `repeats`
    // times; the median submit-to-fence milliseconds divided by `inner`. Batching amortises the
    // submission and fence latency (about 0.5 ms on the RTX 4090) over the inner repeats.
    bool time(int repeats, double& medianMs, std::size_t first = 0, std::size_t last = ~std::size_t(0), int inner = 1);
    // The steps [first, last) between two GPU timestamps, one copy a submission, `repeats` times;
    // the median GPU milliseconds. False where the backend records no timestamps.
    bool timeStamps(int repeats, double& medianMs, std::size_t first = 0, std::size_t last = ~std::size_t(0));
    std::size_t steps() const { return steps_.size(); }
    const std::string& error() const { return err_; }
private:
    struct Buf { rhi::BufferHandle h; std::uint64_t bytes{0}; rhi::Access state{rhi::Access::Undefined}; std::vector<std::uint8_t> init; };
    struct Step { rhi::PipelineHandle pipe; std::vector<int> binds; std::vector<rhi::Access> use; std::uint32_t gx{1}, gy{1}; };
    rhi::Device& dev_;
    std::string err_;
    std::vector<Buf> bufs_;
    std::vector<Step> steps_;
    std::map<std::string, rhi::PipelineHandle> pipes_;
    bool uploaded_{false};
    void move(rhi::CommandList& c, int b, rhi::Access to);
    void record(rhi::CommandList& c, const Step& s);
};

inline std::uint32_t groupsX(std::uint32_t n) { const std::uint32_t g = (n + 63) / 64; return g < 32768u ? g : 32768u; }
inline std::uint32_t groupsY(std::uint32_t n) { return ((n + 63) / 64 + 32767u) / 32768u; }

}  // namespace raw::gpu_check::art_gpu

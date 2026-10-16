// The D3D12 backend's swapchain (N2, evidence/n2-presentation-bounds.json): a flip-discard
// swapchain on a window. Its backbuffers are texture records of the device, so render passes,
// copies and the read-back before Present use the ordinary RHI paths. Each backbuffer keeps one
// RTV slot reserved at creation; a resize drops the records, resizes and adopts the new buffers
// into the same slots.
#include "d3d12_device.hpp"
#include <vector>
namespace raw::rhi::d3d12 {
namespace {
DXGI_COLOR_SPACE_TYPE colourSpaceOf(ColourSpace c){
    switch (c){
        case ColourSpace::Hdr10Pq: return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        case ColourSpace::ScRgbLinear: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        default: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
}
// The refresh rate of the monitor that holds most of the window, from the display
// configuration's exact rational (DXGI outputs of WARP do not describe the real monitor).
double monitorRefreshHz(HWND hwnd){
    MONITORINFOEXW mi{}; mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return 0;
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np); std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return 0;
    for (UINT32 i = 0; i < np; ++i){
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME; src.header.size = sizeof src;
        src.header.adapterId = paths[i].sourceInfo.adapterId; src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        if (wcscmp(src.viewGdiDeviceName, mi.szDevice) != 0) continue;
        const DISPLAYCONFIG_RATIONAL r = paths[i].targetInfo.refreshRate;
        return r.Denominator ? double(r.Numerator) / double(r.Denominator) : 0;
    }
    return 0;
}

class D3d12Swapchain final : public Swapchain {
public:
    explicit D3d12Swapchain(D3d12Device& dev) : dev_(dev) {}
    ~D3d12Swapchain() override {
        dev_.drain();   // Presents still queued hold the buffers and the queue
        releaseBuffers();
        if (waitable_) CloseHandle(waitable_);
    }
    bool init(const SwapchainDesc& d, std::string& err);
    TextureHandle current() override {
        return buffers_.empty() ? TextureHandle{} : buffers_[sc_->GetCurrentBackBufferIndex() % buffers_.size()];
    }
    uint32_t width() const override { return w_; }
    uint32_t height() const override { return h_; }
    TextureFormat format() const override { return format_; }
    bool colourSpaceApplied() const override { return csApplied_; }
    bool tearingAllowed() const override { return tearing_; }
    void prepare(CommandList& cmd) override {
        if (TexRec* t = dev_.texture(current())) static_cast<D3d12CommandList&>(cmd).transition(*t, D3D12_RESOURCE_STATE_PRESENT);
    }
    PresentStatus present(uint32_t syncInterval, std::string& err) override;
    bool resize(uint32_t w, uint32_t h, std::string& err) override;
    bool waitForFrame(uint32_t timeoutMs) override {
        return !waitable_ || WaitForSingleObjectEx(waitable_, timeoutMs, TRUE) == WAIT_OBJECT_0;
    }
    PresentStats stats() override {
        DXGI_FRAME_STATISTICS fs{};
        if (FAILED(sc_->GetFrameStatistics(&fs))) return {};
        return {true, fs.PresentCount, fs.PresentRefreshCount, fs.SyncRefreshCount, fs.SyncQPCTime.QuadPart};
    }
    double refreshHz() override { return monitorRefreshHz(hwnd_); }

private:
    bool adoptBuffers(std::string& err);
    void releaseBuffers(){
        for (TextureHandle h : buffers_) dev_.destroyTexture(h);
        buffers_.clear();
    }
    bool applyColourSpace();
    D3d12Device& dev_;
    ComPtr<IDXGISwapChain3> sc_;
    std::vector<TextureHandle> buffers_;
    std::vector<UINT> rtvs_;
    HWND hwnd_{};
    HANDLE waitable_{};
    uint32_t w_{0}, h_{0};
    TextureFormat format_{TextureFormat::RGBA8Unorm};
    ColourSpace colour_{ColourSpace::SrgbNonlinear};
    UINT flags_{0};
    bool tearing_{false}, csApplied_{false};
};

bool D3d12Swapchain::init(const SwapchainDesc& d, std::string& err){
    if (!d.window || d.width == 0 || d.height == 0){ err = "d3d12: a swapchain needs a window and a non-empty size"; return false; }
    if (d.format != TextureFormat::RGBA8Unorm && d.format != TextureFormat::RGB10A2Unorm && d.format != TextureFormat::RGBA16Float){
        err = "d3d12: swapchain formats are RGBA8Unorm, RGB10A2Unorm and RGBA16Float"; return false; }
    if (d.bufferCount < 2 || d.bufferCount > 4){ err = "d3d12: a flip-model swapchain has 2 to 4 buffers"; return false; }
    hwnd_ = (HWND)d.window; w_ = d.width; h_ = d.height; format_ = d.format; colour_ = d.colourSpace;
    ComPtr<IDXGIFactory5> f;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&f));
    if (FAILED(hr)){ err = hrError("CreateDXGIFactory2 (swapchain)", hr); return false; }
    if (d.allowTearing){
        BOOL allow = FALSE;
        tearing_ = SUCCEEDED(f->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow)) && allow;
    }
    flags_ = (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) | (d.waitable ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = w_; sd.Height = h_; sd.Format = dxgiFormat(d.format);
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = d.bufferCount;
    sd.Scaling = DXGI_SCALING_STRETCH; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sd.Flags = flags_;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(hr = f->CreateSwapChainForHwnd(dev_.queue(), hwnd_, &sd, nullptr, nullptr, &sc1))){
        err = hrError("CreateSwapChainForHwnd", hr); return false; }
    if (FAILED(hr = sc1.As(&sc_))){ err = hrError("IDXGISwapChain3", hr); return false; }
    // Alt+Enter goes to the host, which uses a borderless window instead of exclusive mode.
    f->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    if (d.waitable){
        sc_->SetMaximumFrameLatency(1);
        waitable_ = sc_->GetFrameLatencyWaitableObject();
    }
    rtvs_.resize(d.bufferCount);
    for (UINT& slot : rtvs_) if (!dev_.reserveRtv(slot, err)) return false;
    applyColourSpace();
    return adoptBuffers(err);
}
bool D3d12Swapchain::applyColourSpace(){
    const DXGI_COLOR_SPACE_TYPE cs = colourSpaceOf(colour_);
    UINT support = 0;
    csApplied_ = SUCCEEDED(sc_->CheckColorSpaceSupport(cs, &support))
              && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
              && SUCCEEDED(sc_->SetColorSpace1(cs));
    return csApplied_;
}
bool D3d12Swapchain::adoptBuffers(std::string& err){
    for (UINT i = 0; i < (UINT)rtvs_.size(); ++i){
        TexRec t;
        HRESULT hr = sc_->GetBuffer(i, IID_PPV_ARGS(&t.res));
        if (FAILED(hr)){ err = hrError("swapchain GetBuffer", hr); releaseBuffers(); return false; }
        t.w = w_; t.h = h_; t.format = format_; t.usage = TextureUsage::RenderTarget | TextureUsage::CopySrc;
        t.state = D3D12_RESOURCE_STATE_PRESENT; t.rtv = rtvs_[i];
        dev_.d3d()->CreateRenderTargetView(t.res.Get(), nullptr, cpuAt(dev_.rtvCpu, t.rtv));
        buffers_.push_back(dev_.adoptTexture(std::move(t)));
    }
    return true;
}
PresentStatus D3d12Swapchain::present(uint32_t syncInterval, std::string& err){
    const UINT flags = syncInterval == 0 && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT hr = sc_->Present(syncInterval, flags);
    if (hr == DXGI_STATUS_OCCLUDED) return PresentStatus::Occluded;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || dev_.lost()){
        err = hrError("Present: the device was lost", hr) + "; " + hrError("removed reason", dev_.d3d()->GetDeviceRemovedReason());
        return PresentStatus::DeviceLost;
    }
    if (FAILED(hr)){ err = hrError("Present", hr); return PresentStatus::Failed; }
    return PresentStatus::Presented;
}
bool D3d12Swapchain::resize(uint32_t w, uint32_t h, std::string& err){
    if (w == 0 || h == 0){ err = "d3d12: a swapchain is not resized to an empty size"; return false; }
    dev_.drain();
    releaseBuffers();
    const HRESULT hr = sc_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, flags_);
    if (FAILED(hr)){ err = hrError("ResizeBuffers", hr); return false; }
    w_ = w; h_ = h;
    applyColourSpace();
    return adoptBuffers(err);
}
}  // namespace

std::unique_ptr<Swapchain> D3d12Device::createSwapchain(const SwapchainDesc& desc, std::string& err){
    auto s = std::make_unique<D3d12Swapchain>(*this);
    if (!s->init(desc, err)) return nullptr;
    return s;
}
}  // namespace raw::rhi::d3d12

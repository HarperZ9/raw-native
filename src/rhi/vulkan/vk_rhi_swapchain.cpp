// The Vulkan RHI swapchain (N3): a surface on a Win32, xcb or Wayland window and a VkSwapchainKHR
// whose images are texture records of the device. An image is acquired behind a fence the host
// waits on, so a frame's first barrier needs no semaphore; Present follows the submission's wait.
// Formats: RGBA8Unorm takes R8G8B8A8_UNORM or, where the surface offers only that,
// B8G8R8A8_UNORM, reported as BGRA8Unorm so a read-back can be swizzled.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "vk_rhi.hpp"
#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#endif
#if defined(RAW_NATIVE_VK_XCB)
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#endif
#if defined(RAW_NATIVE_VK_WAYLAND)
#include <wayland-client.h>
#include <vulkan/vulkan_wayland.h>
#endif
#include <algorithm>
#include <cstdint>
namespace raw::rhi::vkr {
namespace {
bool makeSurface(VkrDevice& dev, const SwapchainDesc& d, VkSurfaceKHR& out, std::string& err){
    VkResult r = VK_ERROR_EXTENSION_NOT_PRESENT;
    switch (d.system){
        case WindowSystem::Win32: {
#if defined(_WIN32)
            if (!dev.hasInstanceExtension("VK_KHR_win32_surface")) break;
            VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
            ci.hinstance = GetModuleHandleW(nullptr); ci.hwnd = (HWND)d.window;
            r = vkCreateWin32SurfaceKHR(dev.instance(), &ci, nullptr, &out);
#endif
            break;
        }
        case WindowSystem::Xcb: {
#if defined(RAW_NATIVE_VK_XCB)
            if (!dev.hasInstanceExtension("VK_KHR_xcb_surface")) break;
            VkXcbSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
            ci.connection = (xcb_connection_t*)d.display; ci.window = (xcb_window_t)(uintptr_t)d.window;
            r = vkCreateXcbSurfaceKHR(dev.instance(), &ci, nullptr, &out);
#endif
            break;
        }
        case WindowSystem::Wayland: {
#if defined(RAW_NATIVE_VK_WAYLAND)
            if (!dev.hasInstanceExtension("VK_KHR_wayland_surface")) break;
            VkWaylandSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
            ci.display = (wl_display*)d.display; ci.surface = (wl_surface*)d.window;
            r = vkCreateWaylandSurfaceKHR(dev.instance(), &ci, nullptr, &out);
#endif
            break;
        }
    }
    if (r != VK_SUCCESS){ err = vkError("surface creation (this build or driver lacks the window system's surface extension?)", r); return false; }
    return true;
}

class VkrSwapchain final : public Swapchain {
public:
    explicit VkrSwapchain(VkrDevice& d) : dev_(d) {}
    ~VkrSwapchain() override {
        dev_.waitIdle();
        dropImages();
        if (sc_) vkDestroySwapchainKHR(dev_.vk(), sc_, nullptr);
        if (fence_) vkDestroyFence(dev_.vk(), fence_, nullptr);
        if (surface_) vkDestroySurfaceKHR(dev_.instance(), surface_, nullptr);
    }
    bool init(const SwapchainDesc& d, std::string& err);
    TextureHandle current() override;
    uint32_t width() const override { return w_; }
    uint32_t height() const override { return h_; }
    TextureFormat format() const override { return format_; }
    bool colourSpaceApplied() const override { return csApplied_; }
    bool tearingAllowed() const override { return mode_ != VK_PRESENT_MODE_FIFO_KHR; }
    void prepare(CommandList& cmd) override {
        if (TexRec* t = dev_.texture(current())) static_cast<VkrCommandList&>(cmd).transition(*t, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    PresentStatus present(uint32_t syncInterval, std::string& err) override;
    bool resize(uint32_t w, uint32_t h, std::string& err) override { dev_.waitIdle(); return build(w, h, err); }
    bool waitForFrame(uint32_t) override { return true; }
    PresentStats stats() override { return {}; }   // no frame statistics without VK_GOOGLE_display_timing
    double refreshHz() override { return 0; }

private:
    bool build(uint32_t w, uint32_t h, std::string& err);
    void dropImages(){ for (TextureHandle t : images_) dev_.destroyTexture(t); images_.clear(); }
    VkrDevice& dev_;
    SwapchainDesc desc_;
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkSwapchainKHR sc_{VK_NULL_HANDLE};
    VkFence fence_{VK_NULL_HANDLE};
    VkSurfaceFormatKHR chosen_{};
    VkPresentModeKHR mode_{VK_PRESENT_MODE_FIFO_KHR};
    std::vector<TextureHandle> images_;
    uint32_t w_{0}, h_{0}, index_{0};
    bool acquired_{false}, csApplied_{false};
    TextureFormat format_{TextureFormat::RGBA8Unorm};
};

bool VkrSwapchain::init(const SwapchainDesc& d, std::string& err){
    desc_ = d;
    if (!d.window || d.width == 0 || d.height == 0){ err = "vulkan: a swapchain needs a window and a non-empty size"; return false; }
    if (!makeSurface(dev_, d, surface_, err)) return false;
    VkBool32 ok = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(dev_.physical(), dev_.family(), surface_, &ok);
    if (!ok){ err = "vulkan: the graphics queue cannot present to this surface"; return false; }
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev_.physical(), surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev_.physical(), surface_, &n, fmts.data());
    std::vector<std::pair<VkFormat, VkColorSpaceKHR>> want;
    if (d.format == TextureFormat::RGBA8Unorm) want = {{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}, {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    else if (d.format == TextureFormat::RGB10A2Unorm) want = {{VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT}};
    else if (d.format == TextureFormat::RGBA16Float) want = {{VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT}};
    for (const auto& w : want){
        for (const auto& f : fmts) if (f.format == w.first && f.colorSpace == w.second){ chosen_ = f; break; }
        if (chosen_.format != VK_FORMAT_UNDEFINED) break;
    }
    if (chosen_.format == VK_FORMAT_UNDEFINED){ err = "vulkan: the surface offers no format and colour space for this swapchain request (" + std::to_string(n) + " offered)"; return false; }
    format_ = chosen_.format == VK_FORMAT_B8G8R8A8_UNORM ? TextureFormat::BGRA8Unorm : d.format;
    csApplied_ = true;   // a format is taken only together with its requested colour space
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev_.physical(), surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev_.physical(), surface_, &n, modes.data());
    if (d.allowTearing){
        for (VkPresentModeKHR m : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            if (mode_ == VK_PRESENT_MODE_FIFO_KHR && std::find(modes.begin(), modes.end(), m) != modes.end()) mode_ = m;
    }
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    const VkResult r = vkCreateFence(dev_.vk(), &fc, nullptr, &fence_);
    if (r != VK_SUCCESS){ err = vkError("vkCreateFence (acquire)", r); return false; }
    return build(d.width, d.height, err);
}

bool VkrSwapchain::build(uint32_t w, uint32_t h, std::string& err){
    if (w == 0 || h == 0){ err = "vulkan: a swapchain is not resized to an empty size"; return false; }
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev_.physical(), surface_, &caps);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu){   // the window takes its size from the swapchain (Wayland)
        ext.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0){ err = "vulkan: the surface is empty (minimised)"; return false; }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)){ err = "vulkan: swapchain images cannot be copied from on this surface"; return false; }
    uint32_t count = std::max(caps.minImageCount, desc_.bufferCount);
    if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_; ci.minImageCount = count; ci.imageFormat = chosen_.format; ci.imageColorSpace = chosen_.colorSpace;
    ci.imageExtent = ext; ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                      : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1u));
    ci.presentMode = mode_; ci.clipped = VK_TRUE; ci.oldSwapchain = sc_;
    VkSwapchainKHR next = VK_NULL_HANDLE;
    VkResult r = vkCreateSwapchainKHR(dev_.vk(), &ci, nullptr, &next);
    dropImages();
    if (sc_) vkDestroySwapchainKHR(dev_.vk(), sc_, nullptr);
    sc_ = next; acquired_ = false;
    if (r != VK_SUCCESS){ err = vkError("vkCreateSwapchainKHR", r); return false; }
    w_ = ext.width; h_ = ext.height;
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(dev_.vk(), sc_, &n, nullptr);
    std::vector<VkImage> imgs(n);
    vkGetSwapchainImagesKHR(dev_.vk(), sc_, &n, imgs.data());
    for (VkImage img : imgs){
        TexRec t;
        t.img = img; t.owned = false; t.w = w_; t.h = h_; t.format = format_;
        t.usage = TextureUsage::RenderTarget | TextureUsage::CopySrc;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = chosen_.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if ((r = vkCreateImageView(dev_.vk(), &vi, nullptr, &t.view)) != VK_SUCCESS){ err = vkError("vkCreateImageView (swapchain)", r); return false; }
        images_.push_back(dev_.adoptTexture(t));
    }
    return true;
}

TextureHandle VkrSwapchain::current(){
    if (!acquired_){
        const VkResult r = vkAcquireNextImageKHR(dev_.vk(), sc_, 1000000000ull, VK_NULL_HANDLE, fence_, &index_);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR){ if (r == VK_ERROR_DEVICE_LOST) dev_.markLost(); return {}; }
        vkWaitForFences(dev_.vk(), 1, &fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(dev_.vk(), 1, &fence_);
        acquired_ = true;
    }
    return index_ < images_.size() ? images_[index_] : TextureHandle{};
}

PresentStatus VkrSwapchain::present(uint32_t, std::string& err){
    if (!acquired_){ err = "vulkan: Present without an acquired image"; return PresentStatus::Failed; }
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.swapchainCount = 1; pi.pSwapchains = &sc_; pi.pImageIndices = &index_;
    const VkResult r = vkQueuePresentKHR(dev_.queue(), &pi);
    acquired_ = false;
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) return PresentStatus::Presented;
    if (r == VK_ERROR_DEVICE_LOST){ dev_.markLost(); err = vkError("vkQueuePresentKHR: the device was lost", r); return PresentStatus::DeviceLost; }
    err = vkError("vkQueuePresentKHR", r) + (r == VK_ERROR_OUT_OF_DATE_KHR ? " (out of date: the window changed size without a resize)" : "");
    return PresentStatus::Failed;
}
}  // namespace

std::unique_ptr<Swapchain> VkrDevice::createSwapchain(const SwapchainDesc& desc, std::string& err){
    auto s = std::make_unique<VkrSwapchain>(*this);
    if (!s->init(desc, err)) return nullptr;
    return s;
}
}  // namespace raw::rhi::vkr

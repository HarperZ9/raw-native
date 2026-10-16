#pragma once
// Private to src/platform/linux/: the two Linux windows of raw/platform/window.hpp.
#include "raw/platform/window.hpp"
namespace raw::platform {
std::unique_ptr<Window> createXcbWindow(const WindowDesc& desc, std::string& err);
std::unique_ptr<Window> createWaylandWindow(const WindowDesc& desc, std::string& err);
}  // namespace raw::platform

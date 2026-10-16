// The Linux window of raw/platform/window.hpp: X11 through xcb or Wayland through xdg-shell.
// RAW_NATIVE_WINDOW=x11 or wayland chooses; otherwise Wayland when WAYLAND_DISPLAY is set, else X11.
#include "window_linux.hpp"
#include <cstdlib>
#include <cstring>
namespace raw::platform {
std::unique_ptr<Window> createWindow(const WindowDesc& desc, std::string& err){
    const char* pick = std::getenv("RAW_NATIVE_WINDOW");
    const bool wayland = pick ? std::strcmp(pick, "wayland") == 0 : std::getenv("WAYLAND_DISPLAY") != nullptr;
    if (pick && std::strcmp(pick, "wayland") != 0 && std::strcmp(pick, "x11") != 0){
        err = std::string("RAW_NATIVE_WINDOW must be x11 or wayland, not ") + pick; return nullptr; }
    return wayland ? createWaylandWindow(desc, err) : createXcbWindow(desc, err);
}
}  // namespace raw::platform

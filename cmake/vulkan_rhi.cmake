# N3 (project-docs/RELEASE-PLAN.md; evidence/n3-vulkan-raster-bounds.json): the RHI on Vulkan,
# src/rhi/vulkan/vk_rhi_*.cpp, and raw_native_present_vk. Off by default; the default build is
# unchanged. With RAW_NATIVE_VULKAN_RHI=ON:
#   Windows  the Vulkan SDK's loader, headers and glslangValidator, and the Win32 window.
#   Linux    libvulkan-dev and glslang-tools; libxcb for X11; libwayland-client, wayland-scanner and
#            wayland-protocols for Wayland (xdg-shell, generated at build time). These are the window
#            systems' own client libraries, linked only into this target, never vendored.
option(RAW_NATIVE_VULKAN_RHI "Build the Vulkan RHI backend and raw_native_present_vk" OFF)
if(NOT RAW_NATIVE_VULKAN_RHI OR EMSCRIPTEN)
    return()
endif()
find_package(Vulkan REQUIRED COMPONENTS glslangValidator)
include(cmake/present.cmake)
add_library(raw_native_vulkan STATIC src/rhi/vulkan/vk_rhi_device.cpp src/rhi/vulkan/vk_rhi_buffers.cpp src/rhi/vulkan/vk_rhi_resources.cpp
    src/rhi/vulkan/vk_rhi_commands.cpp src/rhi/vulkan/vk_rhi_swapchain.cpp)
target_link_libraries(raw_native_vulkan PUBLIC raw_native Vulkan::Vulkan)
if(WIN32)
    raw_native_add_present(_vk raw_native_vulkan spirv "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/window_win32.cpp" user32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RAW_XCB REQUIRED IMPORTED_TARGET xcb)
    pkg_check_modules(RAW_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
    pkg_check_modules(RAW_WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
    pkg_get_variable(raw_wl_protocols wayland-protocols pkgdatadir)
    find_program(RAW_WAYLAND_SCANNER wayland-scanner REQUIRED)
    set(wlgen "${CMAKE_CURRENT_BINARY_DIR}/gen-wayland")
    set(xdg "${raw_wl_protocols}/stable/xdg-shell/xdg-shell.xml")
    add_custom_command(OUTPUT "${wlgen}/xdg-shell-client-protocol.h" "${wlgen}/xdg-shell-protocol.c"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${wlgen}"
        COMMAND "${RAW_WAYLAND_SCANNER}" client-header "${xdg}" "${wlgen}/xdg-shell-client-protocol.h"
        COMMAND "${RAW_WAYLAND_SCANNER}" private-code "${xdg}" "${wlgen}/xdg-shell-protocol.c"
        DEPENDS "${xdg}" VERBATIM COMMENT "wayland-scanner xdg-shell")
    enable_language(C)
    target_compile_definitions(raw_native_vulkan PRIVATE RAW_NATIVE_VK_XCB=1 RAW_NATIVE_VK_WAYLAND=1)
    target_link_libraries(raw_native_vulkan PUBLIC PkgConfig::RAW_XCB PkgConfig::RAW_WAYLAND)
    set(lin "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux")
    raw_native_add_present(_vk raw_native_vulkan spirv
        "${lin}/window_linux.cpp;${lin}/window_xcb.cpp;${lin}/window_wayland.cpp;${wlgen}/xdg-shell-protocol.c;${wlgen}/xdg-shell-client-protocol.h"
        "PkgConfig::RAW_XCB;PkgConfig::RAW_WAYLAND")
    target_include_directories(raw_native_present_host_vk PRIVATE "${wlgen}")
endif()
message(STATUS "raw-native: Vulkan RHI on (${Vulkan_LIBRARY}), shaders compiled by glslang")

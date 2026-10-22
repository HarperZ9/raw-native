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
add_library(raw_native_vulkan STATIC src/rhi/vulkan/vk_rhi_device.cpp src/rhi/vulkan/vk_rhi_timestamps.cpp src/rhi/vulkan/vk_rhi_buffers.cpp src/rhi/vulkan/vk_rhi_resources.cpp
    src/rhi/vulkan/vk_rhi_commands.cpp src/rhi/vulkan/vk_rhi_swapchain.cpp src/rhi/vulkan/vk_rhi_spirv.cpp)
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

# N3 criteria 2 and 3 (evidence/n3-vulkan-compute-bounds.json): the renderer's passes in SPIR-V and a
# CLI on the Vulkan backend, raw_native_cli_vk. Each pass is the HLSL that scripts/wgsl_to_hlsl.py
# generates from the one WGSL source (src/renderer/gpu/shaders/hlsl/), compiled by glslang's HLSL
# front end; nothing is translated twice and no pass is edited. Every file with [numthreads] is a
# compute pass entered at main, with binding i at register u<i> (b0 at 0). Every other file is a
# raster pass with entries vs and fs, whose registers count from 0 per kind in the RHI's layout
# order (uniforms, textures, samplers, storage), so each kind is shifted past the kinds before it.
function(raw_native_spirv_shaders gen out_headers)
    set(dir "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/gpu/shaders/hlsl")
    file(GLOB hlsl CONFIGURE_DEPENDS "${dir}/*.hlsl")
    set(headers "")
    set(includes "")
    set(table "")
    foreach(src ${hlsl})
        get_filename_component(p "${src}" NAME_WE)
        file(STRINGS "${src}" compute REGEX "numthreads")
        if(compute)
            set(h "${gen}/spv_${p}.h")
            add_custom_command(OUTPUT "${h}"
                COMMAND Vulkan::glslangValidator -V -D -S comp -e main --target-env vulkan1.3 --vn "kSpv_${p}" -o "${h}" "${src}"
                DEPENDS "${src}" VERBATIM COMMENT "glslang ${p}.hlsl")
            list(APPEND headers "${h}")
            string(APPEND includes "#include \"spv_${p}.h\"\n")
            string(APPEND table "    {\"${p}\", kSpv_${p}, sizeof kSpv_${p}},\n")
            continue()
        endif()
        file(STRINGS "${src}" b REGEX "register[(]b[0-9]+[)]")
        file(STRINGS "${src}" t REGEX "register[(]t[0-9]+[)]")
        file(STRINGS "${src}" s REGEX "register[(]s[0-9]+[)]")
        list(LENGTH b nb)
        list(LENGTH t nt)
        list(LENGTH s ns)
        math(EXPR ts "${nb}")
        math(EXPR ss "${nb} + ${nt}")
        math(EXPR us "${nb} + ${nt} + ${ns}")
        foreach(stage vs fs)
            if(stage STREQUAL "vs")
                set(vk vert)
            else()
                set(vk frag)
            endif()
            set(h "${gen}/spv_${p}_${stage}.h")
            add_custom_command(OUTPUT "${h}"
                COMMAND Vulkan::glslangValidator -V -D -S ${vk} --sep ${stage} -e main --target-env vulkan1.3
                        --shift-texture-binding ${vk} ${ts} --shift-sampler-binding ${vk} ${ss} --shift-ssbo-binding ${vk} ${us}
                        --vn "kSpv_${p}_${stage}" -o "${h}" "${src}"
                DEPENDS "${src}" VERBATIM COMMENT "glslang ${p}.hlsl (${stage})")
            list(APPEND headers "${h}")
            string(APPEND includes "#include \"spv_${p}_${stage}.h\"\n")
            string(APPEND table "    {\"${p}.${stage}\", kSpv_${p}_${stage}, sizeof kSpv_${p}_${stage}},\n")
        endforeach()
    endforeach()
    file(WRITE "${gen}/raw_spirv_shaders.hpp.tmp"
        "#pragma once\n// Generated by cmake/vulkan_rhi.cmake. Do not edit.\n#include <cstddef>\n#include <cstdint>\n${includes}"
        "struct SpirvBlob { const char* name; const uint32_t* words; std::size_t bytes; };\n"
        "inline constexpr SpirvBlob kSpirv[] = {\n${table}};\n")
    configure_file("${gen}/raw_spirv_shaders.hpp.tmp" "${gen}/raw_spirv_shaders.hpp" COPYONLY)
    set(${out_headers} ${headers} PARENT_SCOPE)
endfunction()
set(spvgen "${CMAKE_CURRENT_BINARY_DIR}/gen-spirv")
raw_native_spirv_shaders("${spvgen}" spv_headers)
add_executable(raw_native_cli_vk app/main.cpp src/renderer/gpu/shaders_spirv.cpp src/rhi/null/hw_null.cpp ${spv_headers})
target_include_directories(raw_native_cli_vk PRIVATE "${spvgen}")
target_link_libraries(raw_native_cli_vk PRIVATE raw_native_vulkan)
message(STATUS "raw-native: Vulkan RHI on (${Vulkan_LIBRARY}), shaders compiled by glslang")

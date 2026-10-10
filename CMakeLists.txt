cmake_minimum_required(VERSION 3.24)
project(raw_native VERSION 0.6.0
    DESCRIPTION "CPU renderer that checks fast ambient occlusion against a ray-traced reference"
    HOMEPAGE_URL "https://github.com/HarperZ9/raw-native"
    LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RAW_NATIVE_BUILD_TESTS "Build the raw-native unit tests" ${PROJECT_IS_TOP_LEVEL})

# The library: every layer except the RHI backends and the shader libraries,
# which a build picks in cmake/gpu.cmake (docs/architecture/ARCHITECTURE.md).
# scripts/check_layers.py enforces which layer may include which.
set(RAW_NATIVE_LIB_LAYERS core cert scene assets graph renderer tools)
set(LIB_SRCS "")
foreach(layer ${RAW_NATIVE_LIB_LAYERS})
    file(GLOB layer_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/${layer}/*.cpp")
    list(APPEND LIB_SRCS ${layer_srcs})
endforeach()
# The GPU renderer is backend-neutral: it records a frame graph on the RHI.
list(APPEND LIB_SRCS src/renderer/gpu/gpu_renderer.cpp src/renderer/gpu/frame_pack.cpp src/renderer/gpu/texture_identity.cpp src/renderer/gpu/pbr_parity.cpp
    src/renderer/gpu/lighting_parity.cpp src/renderer/gpu/lighting_parity_pack.cpp
    src/renderer/gpu/raster_identity.cpp src/renderer/gpu/gbuffer_identity.cpp
    src/renderer/gpu/shadow_parity.cpp src/renderer/gpu/shadow_parity_gpu.cpp
    src/renderer/gpu/post_parity.cpp
    src/renderer/gpu/swr_gpu.cpp src/renderer/gpu/swr_parity.cpp)
add_library(raw_native STATIC ${LIB_SRCS})
add_library(raw_native::raw_native ALIAS raw_native)
target_include_directories(raw_native PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:include>")
target_compile_features(raw_native PUBLIC cxx_std_23)
# superstack.hpp (FSL-1.1-MIT, vendored and pinned in third_party/superstack)
# builds the receipt; it is private to the library and not installed.
target_include_directories(raw_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/superstack")
# Receipts need the header to compile with this standard library. superstack
# v0.2.0 builds with libstdc++, MSVC and libc++ (Emscripten); v0.1.0 did not
# build with libc++. The vendored copy must not be patched, so a toolchain where
# the header fails builds without receipts and says so at configure and run
# time. CI requires a receipt from every build it runs, the wasm build included.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/superstack")
check_cxx_source_compiles("#include \"superstack.hpp\"
int main(){ return superstack::canonical(superstack::Value(1)).size() == 1 ? 0 : 1; }" RAW_NATIVE_SUPERSTACK_COMPILES)
unset(CMAKE_REQUIRED_INCLUDES)
if(RAW_NATIVE_SUPERSTACK_COMPILES)
    target_compile_definitions(raw_native PRIVATE RAW_NATIVE_SUPERSTACK=1)
else()
    message(WARNING "superstack.hpp does not compile with this standard library; receipt.json and gpu_receipt.json will not be written")
endif()
# Strip the build machine's source path from anything the compiler embeds, so
# two checkouts in different directories produce the same binaries.
target_compile_options(raw_native PUBLIC
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")
set(RAW_NATIVE_LICENSE "FSL-1.1-MIT")   # SPDX id; 0.2.0 and earlier were released under MIT
if(EMSCRIPTEN)
    include(cmake/wasm.cmake)
elseif(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(raw_native PUBLIC Threads::Threads)
endif()

# The RHI backend and shader library of a build without a GPU. Tests link it
# so they build the same way in every configuration.
set(RAW_NATIVE_NULL_BACKEND
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/null/null_device.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/gpu/shaders_none.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/null/hw_null.cpp")

if(RAW_NATIVE_BUILD_TESTS)
    enable_testing()
    file(GLOB TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
    foreach(t ${TEST_SRCS})
        get_filename_component(name ${t} NAME_WE)
        if(name STREQUAL "test_receipt" AND NOT RAW_NATIVE_SUPERSTACK_COMPILES)
            continue()
        endif()
        add_executable(${name} ${t} ${RAW_NATIVE_NULL_BACKEND})
        target_link_libraries(${name} PRIVATE raw_native)
        target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/superstack")
        target_compile_definitions(${name} PRIVATE RAW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
    # The glTF fuzzer (ROADMAP M2 criterion 2). ctest runs a short pass; CI runs a
    # million inputs under the sanitizers.
    add_executable(fuzz_gltf tests/fuzz_gltf.cpp ${RAW_NATIVE_NULL_BACKEND})
    target_link_libraries(fuzz_gltf PRIVATE raw_native)
    add_test(NAME fuzz_gltf_short COMMAND fuzz_gltf 20000 7)
    # The superstack contract's own vectors, run against the vendored header on
    # this compiler. Fails when the header or a vector file no longer matches.
    if(RAW_NATIVE_SUPERSTACK_COMPILES)
    add_executable(superstack_vectors third_party/superstack/tests/run_vectors.cpp)
    target_include_directories(superstack_vectors PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/superstack")
    target_compile_features(superstack_vectors PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(superstack_vectors PRIVATE /utf-8 /EHsc)
    endif()
    add_test(NAME superstack_vectors COMMAND superstack_vectors "${CMAKE_CURRENT_SOURCE_DIR}/third_party/superstack")
    endif()
endif()

add_executable(raw_native_cli app/main.cpp)
target_link_libraries(raw_native_cli PRIVATE raw_native)
# Adds the CLI's RHI backend: the null backend by default, the D3D12 backend
# with RAW_NATIVE_GPU_D3D12=ON on Windows. Also defines the WebGPU wasm target.
include(cmake/gpu.cmake)
if(EMSCRIPTEN)
    raw_native_wasm_target(raw_native_cli)
endif()

# Install the CLI, the static library, its headers and a CMake package so other
# projects can use find_package(raw_native CONFIG) and link raw_native::raw_native.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS raw_native_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS raw_native EXPORT raw_nativeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION include)
install(DIRECTORY raw DESTINATION include FILES_MATCHING PATTERN "*.hpp")
install(EXPORT raw_nativeTargets NAMESPACE raw_native::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/raw_native)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/raw_nativeConfig.cmake"
    "set(raw_native_LICENSE \"${RAW_NATIVE_LICENSE}\")\n"
    "include(CMakeFindDependencyMacro)\n"
    "if(NOT WIN32)\n  find_dependency(Threads)\nendif()\n"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/raw_nativeTargets.cmake\")\n")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/raw_nativeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/raw_nativeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/raw_nativeConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/raw_native)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/raw_native)

# WebAssembly build settings (Emscripten). Included from CMakeLists.txt when
# the Emscripten toolchain file is in use. Every flag that changes the output
# is pinned here so the same emsdk version produces the same bytes.
#
# The CMake version is pinned too. The released wasm files are built with
# CMake 4.2.0 and emsdk 6.0.11. CMake's Emscripten platform files changed in
# 4.2, which adds -fPIC to every compile, so a build configured with another
# CMake (3.29, for example) gives different wasm bytes from the same source.
# Another version still builds and still writes the same output files; it
# warns here because its wasm will not match the release's SHA256SUMS.
set(RAW_NATIVE_WASM_CMAKE_VERSION "4.2.0")
if(NOT CMAKE_VERSION VERSION_EQUAL RAW_NATIVE_WASM_CMAKE_VERSION)
    message(WARNING "The release wasm is built with CMake ${RAW_NATIVE_WASM_CMAKE_VERSION}; "
        "this is CMake ${CMAKE_VERSION}, so raw-native.wasm will not match the released bytes")
endif()
#
# Variants:
#   RAW_NATIVE_WASM_SIMD=ON     compile with 128-bit wasm SIMD (-msimd128)
#   RAW_NATIVE_WASM_THREADS=ON  pthreads build; needs SharedArrayBuffer, which a
#                               browser only grants to cross-origin isolated pages
option(RAW_NATIVE_WASM_SIMD "Build the wasm module with -msimd128" OFF)
option(RAW_NATIVE_WASM_THREADS "Build the wasm module with pthreads" OFF)

set(RAW_NATIVE_WASM_COMPILE -O2 -fwasm-exceptions)
if(RAW_NATIVE_WASM_SIMD)
    list(APPEND RAW_NATIVE_WASM_COMPILE -msimd128)
endif()
if(RAW_NATIVE_WASM_THREADS)
    list(APPEND RAW_NATIVE_WASM_COMPILE -pthread)
endif()
target_compile_options(raw_native PUBLIC ${RAW_NATIVE_WASM_COMPILE})

function(raw_native_wasm_target tgt)
    set_target_properties(${tgt} PROPERTIES OUTPUT_NAME "raw-native" SUFFIX ".mjs")
    target_link_options(${tgt} PRIVATE
        ${RAW_NATIVE_WASM_COMPILE}
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createRawNative
        -sINVOKE_RUN=0
        -sEXIT_RUNTIME=0
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=64MB
        -sSTACK_SIZE=1MB
        -sFORCE_FILESYSTEM=1
        -sENVIRONMENT=web,worker,node
        -sEXPORTED_RUNTIME_METHODS=callMain,FS
        -sFILESYSTEM=1)
    if(RAW_NATIVE_WASM_THREADS)
        target_link_options(${tgt} PRIVATE -pthread -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency)
    endif()
endfunction()

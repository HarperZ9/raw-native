cmake_minimum_required(VERSION 3.24)
project(raw_native VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RAW_NATIVE_BUILD_TESTS "Build the raw-native unit tests" ${PROJECT_IS_TOP_LEVEL})

file(GLOB LIB_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(raw_native STATIC ${LIB_SRCS})
add_library(raw_native::raw_native ALIAS raw_native)
target_include_directories(raw_native PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:include>")
target_compile_features(raw_native PUBLIC cxx_std_23)

if(RAW_NATIVE_BUILD_TESTS)
    enable_testing()
    file(GLOB TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
    foreach(t ${TEST_SRCS})
        get_filename_component(name ${t} NAME_WE)
        add_executable(${name} ${t})
        target_link_libraries(${name} PRIVATE raw_native)
        target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
endif()

add_executable(raw_native_cli app/main.cpp)
target_link_libraries(raw_native_cli PRIVATE raw_native)

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
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/raw_nativeTargets.cmake\")\n")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/raw_nativeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/raw_nativeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/raw_nativeConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/raw_native)

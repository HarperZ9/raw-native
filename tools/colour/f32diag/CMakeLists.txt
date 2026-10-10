cmake_minimum_required(VERSION 3.20)
project(f32diag CXX)
set(CMAKE_CXX_STANDARD 20)
set(SRC "" CACHE PATH "raw-native source root")
add_executable(f32diag main.cpp
  ${SRC}/src/renderer/aces2_cam.cpp ${SRC}/src/renderer/aces2_gamut.cpp ${SRC}/src/renderer/aces2_tables.cpp
  ${SRC}/src/renderer/colour.cpp ${SRC}/src/renderer/colour_math.cpp)
target_include_directories(f32diag PRIVATE ${SRC} ${SRC}/src/renderer)
# Same floating-point model as the reference build: no fast-math.
target_compile_options(f32diag PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/diagmath.hpp /fp:precise)

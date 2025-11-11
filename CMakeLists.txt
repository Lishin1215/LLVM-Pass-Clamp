cmake_minimum_required(VERSION 3.15)
project(ImgOptPass LANGUAGES C CXX)

set(LLVM_DIR "/usr/lib/llvm-18/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(ImgOptPass SHARED ImgOptPass.cpp)

set_target_properties(ImgOptPass PROPERTIES PREFIX "")
target_link_libraries(ImgOptPass PRIVATE LLVM)

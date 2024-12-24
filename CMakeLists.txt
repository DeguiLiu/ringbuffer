cmake_minimum_required(VERSION 3.14)
project(ringbuffer VERSION 2.0.0 LANGUAGES CXX)

# --- Core: header-only interface library ---
add_library(ringbuffer INTERFACE)
target_include_directories(ringbuffer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(ringbuffer INTERFACE cxx_std_14)

# --- Options ---
option(RINGBUFFER_BUILD_EXAMPLES "Build examples" ON)
option(RINGBUFFER_BUILD_TESTS "Build tests" ON)

find_package(Threads REQUIRED)

if(RINGBUFFER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RINGBUFFER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Install ---
include(CMakePackageConfigHelpers)
install(TARGETS ringbuffer EXPORT ringbufferTargets)
install(DIRECTORY include/spsc DESTINATION include)
install(EXPORT ringbufferTargets
    FILE ringbufferConfig.cmake
    NAMESPACE spsc::
    DESTINATION lib/cmake/ringbuffer)

cmake_minimum_required(VERSION 3.16)
project(jpeg_converter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization: benchmarks are only meaningful in Release mode.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# Source files
set(SOURCES
    src/color_converter.cpp
    src/dct.cpp
    src/quantization.cpp
    src/block_processor.cpp
    src/thread_pool.cpp
    src/jpeg_encoder.cpp
    src/benchmark.cpp
)

add_executable(jpeg_converter main.cpp ${SOURCES})

target_include_directories(jpeg_converter PRIVATE include)

# Link pthreads on Linux; on macOS threads are part of libc.
find_package(Threads REQUIRED)
target_link_libraries(jpeg_converter Threads::Threads)

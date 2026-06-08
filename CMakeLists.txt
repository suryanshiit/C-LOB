cmake_minimum_required(VERSION 3.20)
project(HighPerfLOB CXX)

set(CMAKE_CXX_STANDARD 20) # Use C++20 for concepts and designated initializers

# High-Performance Optimization Flags
if(MSVC)
    add_compile_options(/O2 /Oi /Ot /arch:AVX2)
else()
    add_compile_options(-O3 -march=native -ffast-math  -fno-rtti)
endif()

add_executable(LOB_Engine main.cpp
        main.cpp)
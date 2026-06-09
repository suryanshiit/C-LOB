cmake_minimum_required(VERSION 3.20)
project(HighPerfLOB CXX)

set(CMAKE_CXX_STANDARD 20) # Use C++20 for concepts and designated initializers

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Full debug instrumentation — no optimisation, full symbols, sanitizers
    add_compile_options(
            -O0                          # No optimisation: variables stay alive, inlining disabled
            -g3                          # Maximum debug info (includes macro expansions)
            -fno-omit-frame-pointer      # Keep frame pointer so GDB can unwind the stack
            -fno-optimize-sibling-calls  # Prevents tail-call elimination hiding frames
            -fsanitize=address,undefined # ASan + UBSan: catches OOB, use-after-free, UB
    )
    add_link_options(-fsanitize=address,undefined)
# High-Performance Optimization Flags
elseif(MSVC)
    add_compile_options(/O2 /Oi /Ot /arch:AVX2)
else()
    add_compile_options(-O3 -march=native -ffast-math  -fno-rtti)
endif()

add_executable(LOB_Engine main.cpp)
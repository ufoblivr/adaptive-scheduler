cmake_minimum_required(VERSION 3.16)
project(AdaptiveSchedPP VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

add_library(adaptive_sched_lib STATIC
    src/execution/cpu.cpp
    src/metrics/statistics.cpp
    src/scheduler/adaptive.cpp
    src/scheduler/bandit.cpp
    src/simulation/workload.cpp
    src/simulation/simulation_controller.cpp
)
target_include_directories(adaptive_sched_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(adaptive_sched main.cpp)
target_link_libraries(adaptive_sched PRIVATE adaptive_sched_lib)

# ── Tests ────────────────────────────────────────────────────────────────────
option(ADAPTIVE_SCHED_BUILD_TESTS "Build unit tests" ON)
if(ADAPTIVE_SCHED_BUILD_TESTS)
    enable_testing()
    add_executable(sched_tests tests/test_main.cpp)
    target_link_libraries(sched_tests PRIVATE adaptive_sched_lib)
    add_test(NAME sched_tests COMMAND sched_tests)
endif()

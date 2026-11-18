cmake_minimum_required(VERSION 3.20)
project(orbitfall_core CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library: everything under core/ except tests, bench and third_party.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/sim/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pools/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/systems/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/spatial/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/content/*.cpp)
add_library(orbitfall_core STATIC ${CORE_SOURCES})
target_include_directories(orbitfall_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(orbitfall_core PUBLIC ORBITFALL_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../content")

# Strategy bots (M2.9): shared by the balance tests and tools/sim-runner. Not
# part of the sim library and not built into the GDExtension.
file(GLOB BOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bots/*.cpp)
add_library(orbitfall_bots STATIC ${BOT_SOURCES})
target_link_libraries(orbitfall_bots PUBLIC orbitfall_core)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(core_tests ${TEST_SOURCES})
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(core_tests PRIVATE orbitfall_bots orbitfall_core)
# Lets tests scan the core/ sources (banned-call determinism check).
target_compile_definitions(core_tests PRIVATE ORBITFALL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB BENCH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp)
if(BENCH_SOURCES)
  add_executable(core_bench ${BENCH_SOURCES})
  target_link_libraries(core_bench PRIVATE orbitfall_core)
endif()

# Headless bot runner (M2.9): N seeded runs per bot, results as CSV.
add_executable(sim_runner ${CMAKE_CURRENT_SOURCE_DIR}/../tools/sim-runner/main.cpp)
set_target_properties(sim_runner PROPERTIES OUTPUT_NAME sim-runner)
target_link_libraries(sim_runner PRIVATE orbitfall_bots orbitfall_core)

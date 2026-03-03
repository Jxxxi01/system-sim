cmake_minimum_required(VERSION 3.16)

project(simulator_demo LANGUAGES CXX)

# Build/Test quick start:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#   ctest --test-dir build
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(simulator_core STATIC
  src/core/scaffold.cpp
  src/isa/scaffold.cpp
  src/security/scaffold.cpp
  src/kernel/scaffold.cpp
)

target_include_directories(simulator_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TESTING)
  add_executable(test_sanity
    tests/test_sanity.cpp
  )

  target_link_libraries(test_sanity
    PRIVATE
      simulator_core
  )

  add_test(NAME sanity COMMAND test_sanity)
endif()

cmake_minimum_required(VERSION 3.16)

project(simulator_demo LANGUAGES CXX)

# Build/Test quick start:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#   ctest --test-dir build
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    "$<$<CONFIG:Debug>:-O0>"
    "$<$<CONFIG:Debug>:-g3>"
    "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
  )
elseif(MSVC)
  add_compile_options(
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<CONFIG:Debug>:/Zi>"
  )
endif()

include(CTest)

add_library(simulator_core STATIC
  src/core/executor.cpp
  src/core/scaffold.cpp
  src/isa/assembler.cpp
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

  add_executable(test_isa_assembler
    tests/test_isa_assembler.cpp
  )

  target_link_libraries(test_isa_assembler
    PRIVATE
      simulator_core
  )

  add_test(NAME isa_assembler COMMAND test_isa_assembler)

  add_executable(test_executor
    tests/test_executor.cpp
  )

  target_link_libraries(test_executor
    PRIVATE
      simulator_core
  )

  add_test(NAME executor COMMAND test_executor)
endif()

add_executable(demo_normal
  demos/normal/demo_normal.cpp
)

target_link_libraries(demo_normal
  PRIVATE
    simulator_core
)

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
  src/isa/opcode.cpp
  src/kernel/process.cpp
  src/isa/scaffold.cpp
  src/security/code_codec.cpp
  src/security/securir_builder.cpp
  src/security/audit.cpp
  src/security/ewc.cpp
  src/security/gateway.cpp
  src/security/pvt.cpp
  src/security/spe.cpp
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

  add_executable(test_gateway
    tests/test_gateway.cpp
  )

  target_link_libraries(test_gateway
    PRIVATE
      simulator_core
  )

  add_test(NAME gateway COMMAND test_gateway)

  add_executable(test_kernel_process
    tests/test_kernel_process.cpp
  )

  target_link_libraries(test_kernel_process
    PRIVATE
      simulator_core
  )

  add_test(NAME kernel_process COMMAND test_kernel_process)

  add_executable(test_pvt
    tests/test_pvt.cpp
  )

  target_link_libraries(test_pvt
    PRIVATE
      simulator_core
  )

  add_test(NAME pvt COMMAND test_pvt)

  add_executable(test_spe
    tests/test_spe.cpp
  )

  target_link_libraries(test_spe
    PRIVATE
      simulator_core
  )

  add_test(NAME spe COMMAND test_spe)

  add_executable(test_securir_builder
    tests/test_securir_builder.cpp
  )

  target_link_libraries(test_securir_builder
    PRIVATE
      simulator_core
  )

  add_test(NAME securir_builder COMMAND test_securir_builder)
endif()

add_executable(demo_normal
  demos/normal/demo_normal.cpp
)

target_link_libraries(demo_normal
  PRIVATE
    simulator_core
)

add_executable(demo_cross_user
  demos/cross_user/demo_cross_user.cpp
)

target_link_libraries(demo_cross_user
  PRIVATE
    simulator_core
)

add_executable(demo_malicious_mapping
  demos/malicious_mapping/demo_malicious_mapping.cpp
)

target_link_libraries(demo_malicious_mapping
  PRIVATE
    simulator_core
)

add_executable(demo_cross_process
  demos/cross_process/demo_cross_process.cpp
)

target_link_libraries(demo_cross_process
  PRIVATE
    simulator_core
)

add_executable(demo_injection
  demos/injection/demo_injection.cpp
)

target_link_libraries(demo_injection
  PRIVATE
    simulator_core
)

add_executable(demo_rop
  demos/rop/demo_rop.cpp
)

target_link_libraries(demo_rop
  PRIVATE
    simulator_core
)

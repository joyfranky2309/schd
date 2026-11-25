cmake_minimum_required(VERSION 3.20)
project(schd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCHD_ASAN "Build with ASan + UBSan" OFF)
option(SCHD_TSAN "Build with ThreadSanitizer" OFF)
option(SCHD_WITH_QUANTLIB "Enable the optional QuantLib bond job" OFF)

if(SCHD_ASAN AND SCHD_TSAN)
  message(FATAL_ERROR "SCHD_ASAN and SCHD_TSAN cannot be enabled together")
endif()

find_package(Threads REQUIRED)
if(SCHD_WITH_QUANTLIB)
  find_package(QuantLib CONFIG QUIET)
  if(NOT TARGET QuantLib::QuantLib)
    find_path(QL_INCLUDE_DIR ql/quantlib.hpp)
    find_library(QL_LIBRARY NAMES QuantLib)
    if(NOT QL_INCLUDE_DIR OR NOT QL_LIBRARY)
      message(FATAL_ERROR
        "SCHD_WITH_QUANTLIB=ON requires QuantLib headers (ql/quantlib.hpp) "
        "and the QuantLib library")
    endif()
    if(POLICY CMP0167)
      cmake_policy(SET CMP0167 NEW)
    endif()
    find_package(Boost REQUIRED)
    add_library(schd_quantlib UNKNOWN IMPORTED)
    set_target_properties(schd_quantlib PROPERTIES
      IMPORTED_LOCATION "${QL_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${QL_INCLUDE_DIR};${Boost_INCLUDE_DIRS}"
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${QL_INCLUDE_DIR};${Boost_INCLUDE_DIRS}"
    )
    set(SCHD_QUANTLIB_TARGET schd_quantlib)
  else()
    set(SCHD_QUANTLIB_TARGET QuantLib::QuantLib)
  endif()
endif()
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(googletest)
FetchContent_MakeAvailable(nlohmann_json)
include(GoogleTest)

enable_testing()

add_library(schd_core
  src/job_status.cpp
  src/job_run.cpp
  src/stub_job.cpp
  src/failing_stub_job.cpp
  src/shell_job.cpp
  src/cpu_pricing_job.cpp
  src/job_loader.cpp
)
if(SCHD_WITH_QUANTLIB)
  target_sources(schd_core PRIVATE src/quantlib_bond_job.cpp)
  target_compile_definitions(schd_core PUBLIC SCHD_WITH_QUANTLIB=1)
endif()
target_include_directories(schd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(schd_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(schd_core PUBLIC Threads::Threads PRIVATE nlohmann_json::nlohmann_json)
if(SCHD_WITH_QUANTLIB)
  target_link_libraries(schd_core PUBLIC ${SCHD_QUANTLIB_TARGET})
endif()

add_executable(schd src/main.cpp)
target_link_libraries(schd PRIVATE schd_core)
target_compile_options(schd PRIVATE -Wall -Wextra -Wpedantic)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "tests/*.cpp")
add_executable(schd_tests ${TEST_SOURCES})
target_link_libraries(schd_tests PRIVATE schd_core GTest::gtest_main)
target_compile_options(schd_tests PRIVATE -Wall -Wextra -Wpedantic)
add_dependencies(schd_tests schd)
target_compile_definitions(schd_tests PRIVATE SCHD_CLI_PATH="$<TARGET_FILE:schd>")

if(SCHD_ASAN)
  foreach(target schd_core schd schd_tests)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

if(SCHD_TSAN)
  foreach(target schd_core schd schd_tests)
    target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=thread)
  endforeach()
endif()

gtest_discover_tests(schd_tests)

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT SCHD_ASAN AND NOT SCHD_TSAN)
  add_executable(schd_bench bench/bench_dag.cpp)
  target_link_libraries(schd_bench PRIVATE schd_core)
  target_compile_options(schd_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()
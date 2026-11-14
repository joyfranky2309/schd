cmake_minimum_required(VERSION 3.20)
project(schd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCHD_ASAN "Build with ASan + UBSan" OFF)
option(SCHD_TSAN "Build with ThreadSanitizer" OFF)

if(SCHD_ASAN AND SCHD_TSAN)
  message(FATAL_ERROR "SCHD_ASAN and SCHD_TSAN cannot be enabled together")
endif()

find_package(Threads REQUIRED)
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

enable_testing()

add_library(schd_core
  src/job_status.cpp
  src/job_run.cpp
  src/stub_job.cpp
  src/failing_stub_job.cpp
)
target_include_directories(schd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(schd_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(schd_core PUBLIC Threads::Threads)

add_executable(schd src/main.cpp)
target_link_libraries(schd PRIVATE schd_core)
target_compile_options(schd PRIVATE -Wall -Wextra -Wpedantic)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "tests/*.cpp")
add_executable(schd_tests ${TEST_SOURCES})
target_link_libraries(schd_tests PRIVATE schd_core GTest::gtest_main)
target_compile_options(schd_tests PRIVATE -Wall -Wextra -Wpedantic)

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
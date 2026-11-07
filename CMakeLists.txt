cmake_minimum_required(VERSION 3.20)
project(schd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20, not gnu++20
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets VS Code / clangd see real flags

option(SCHD_SANITIZE "Build with ASan + UBSan" OFF)
option(SCHD_TSAN "Build with ThreadSanitizer" OFF)

if(SCHD_SANITIZE AND SCHD_TSAN)
  message(FATAL_ERROR "SCHD_SANITIZE and SCHD_TSAN cannot be enabled together")
endif()

find_package(Threads REQUIRED)

add_executable(schd src/main.cpp src/job_status.cpp src/job_run.cpp src/stub_job.cpp src/failing_stub_job.cpp)

target_compile_options(schd PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(schd PRIVATE Threads::Threads)

if(SCHD_SANITIZE)
  target_compile_options(schd PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(schd PRIVATE -fsanitize=address,undefined)
endif()

if(SCHD_TSAN)
  target_compile_options(schd PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(schd PRIVATE -fsanitize=thread)
endif()
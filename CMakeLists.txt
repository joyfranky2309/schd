cmake_minimum_required(VERSION 3.20)
project(schd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20, not gnu++20
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets VS Code / clangd see real flags

option(SCHD_SANITIZE "Build with ASan + UBSan" OFF)

add_executable(schd src/main.cpp src/job_status.cpp)

target_compile_options(schd PRIVATE -Wall -Wextra -Wpedantic)

if(SCHD_SANITIZE)
  target_compile_options(schd PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(schd PRIVATE -fsanitize=address,undefined)
endif()
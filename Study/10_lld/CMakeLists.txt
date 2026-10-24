cmake_minimum_required(VERSION 3.20)
project(lld_study LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # generates compile_commands.json for editor tooling (clangd etc.)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Sanitizers on by default in Debug builds -- catches the exact bug classes
# this track's concurrency and RAII tutorials warn about (races, double
# frees, use-after-move) the moment they happen, not eventually.
# Disable with: cmake -B build -DENABLE_SANITIZERS=OFF
option(ENABLE_SANITIZERS "Build with ASan+UBSan (or pass -DSANITIZER=thread for TSan)" ON)
set(SANITIZER "address" CACHE STRING "Sanitizer to use: address (default) or thread")

if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
  else()
    add_compile_options(-fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_library(lld_reference INTERFACE)
target_include_directories(lld_reference INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include/reference)

add_subdirectory(src)
add_subdirectory(tests)

cmake_minimum_required(VERSION 3.20)
project(phoenix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

# Optional sanitizer build for the fuzz suite; off by default so the plain
# user-facing build stays vanilla. Enable with -DPHOENIX_SANITIZE=ON.
option(PHOENIX_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
if(PHOENIX_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_subdirectory(core)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sim/CMakeLists.txt)
  add_subdirectory(sim)
endif()

add_library(phoenix_core STATIC
  src/framebuffer.cpp
)

target_include_directories(phoenix_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  add_subdirectory(tests)
endif()

add_executable(phoenix_sim
  src/main.cpp
  src/scenario.cpp
  src/runner.cpp
  src/html_writer.cpp
)

target_link_libraries(phoenix_sim PRIVATE phoenix_core)
target_include_directories(phoenix_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

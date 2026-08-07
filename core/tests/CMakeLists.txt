function(phoenix_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE phoenix_core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${name} PRIVATE
    GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
  add_test(NAME ${name} COMMAND ${name})
endfunction()

phoenix_test(test_framebuffer)
phoenix_test(test_text)
phoenix_test(test_layout)
phoenix_test(test_golden)

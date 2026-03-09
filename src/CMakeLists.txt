add_subdirectory(base)
add_subdirectory(test)
add_subdirectory(frontend)
add_subdirectory(translation)
add_subdirectory(memory_system)
add_subdirectory(addr_mapper)
add_subdirectory(dram)
add_subdirectory(dram_controller)
add_subdirectory(visualizer)

target_link_libraries(
  ramulator-exe 
  PRIVATE 
  visualizer
)

target_sources(
  ramulator-exe
  PRIVATE 
  main.cpp
)

add_library(profiling profiling.cpp)
target_link_libraries(profiling Tracy::TracyClient)
target_include_directories(profiling PUBLIC ${CMAKE_CURRENT_LIST_DIR})

set(FEATURES)

option(BUILD_STANDALONE_EGUI_TEST "Build standalone egui+gfx test" ON)
if(BUILD_STANDALONE_EGUI_TEST)
  list(APPEND FEATURES standalone_test)
endif()

add_library(gfx-egui
  egui_types.cpp
  renderer.cpp
  input.cpp
)
target_link_libraries(gfx-egui gfx)

add_rust_library(
  NAME egui-gfx
  FEATURES ${FEATURES}
  PROJECT_PATH ${CMAKE_CURRENT_LIST_DIR}
  TARGET_PATH ${CMAKE_CURRENT_LIST_DIR}/target
  DEPENDS rust_interop.hpp
)

if(BUILD_STANDALONE_EGUI_TEST)
  add_executable(gfx-egui-test test.cpp)
  target_link_libraries(gfx-egui-test egui-gfx-rust gfx-egui)
endif()

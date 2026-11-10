cmake_minimum_required(VERSION 3.20)
project(imgui-node-editor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(imgui CONFIG REQUIRED)

add_library(imgui-node-editor STATIC crude_json.cpp imgui_canvas.cpp imgui_node_editor.cpp imgui_node_editor_api.cpp)
target_link_libraries(imgui-node-editor PRIVATE imgui::imgui)

if(MSVC)
    target_compile_options(imgui-node-editor PRIVATE /utf-8)
endif()

install(TARGETS imgui-node-editor ARCHIVE DESTINATION lib)
install(FILES imgui_node_editor.h DESTINATION include)

cmake_minimum_required(VERSION 3.2 FATAL_ERROR)
project(simple_digest VERSION 0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Thank ChatGPT for this code to extract the last folder in a path
string(FIND "${CMAKE_BINARY_DIR}" "/" last_separator_index REVERSE)
math(EXPR last_separator_index "${last_separator_index} + 1")
string(SUBSTRING "${CMAKE_BINARY_DIR}" ${last_separator_index} -1 last_folder)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(${last_folder} MATCHES "build")
    message("In correct 'build' folder, continuing...")
else()
    message(FATAL_ERROR "${last_folder} Not in 'build' folder, please run CMake from there with 'cmake ..', you might have to clean CMakeCache files")
endif()

add_subdirectory(control_plane)
add_subdirectory(p4)

add_custom_target(default DEPENDS main simple_digest)

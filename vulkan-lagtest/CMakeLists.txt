cmake_minimum_required(VERSION 3.20)
project(lagtest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

# Manual GLFW find (pkg-config not installed)
find_library(GLFW_LIBRARY glfw PATHS /usr/lib/x86_64-linux-gnu)
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h PATHS /usr/include)
if(NOT GLFW_LIBRARY OR NOT GLFW_INCLUDE_DIR)
    message(FATAL_ERROR "GLFW not found")
endif()

# Compile shaders
set(VERT_SHADER "${CMAKE_SOURCE_DIR}/shader.vert")
set(FRAG_SHADER "${CMAKE_SOURCE_DIR}/shader.frag")
set(VERT_SPV "${CMAKE_BINARY_DIR}/vert.spv")
set(FRAG_SPV "${CMAKE_BINARY_DIR}/frag.spv")

add_custom_command(
    OUTPUT ${VERT_SPV}
    COMMAND glslangValidator -V ${VERT_SHADER} -o ${VERT_SPV}
    DEPENDS ${VERT_SHADER}
)
add_custom_command(
    OUTPUT ${FRAG_SPV}
    COMMAND glslangValidator -V ${FRAG_SHADER} -o ${FRAG_SPV}
    DEPENDS ${FRAG_SHADER}
)
add_custom_target(shaders ALL DEPENDS ${VERT_SPV} ${FRAG_SPV})

add_executable(lagtest main.cpp)
add_dependencies(lagtest shaders)

target_include_directories(lagtest PRIVATE ${GLFW_INCLUDE_DIR})
target_link_libraries(lagtest PRIVATE Vulkan::Vulkan ${GLFW_LIBRARY})

# Copy shaders next to binary for runtime loading
add_custom_command(TARGET lagtest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${VERT_SPV} $<TARGET_FILE_DIR:lagtest>/vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${FRAG_SPV} $<TARGET_FILE_DIR:lagtest>/frag.spv
)

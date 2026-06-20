cmake_minimum_required(VERSION 3.20)
project(lagtest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Try CMake's FindVulkan first, fallback to manual find
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    find_library(VULKAN_LIBRARY vulkan
        PATHS /usr/lib /usr/lib64 /usr/local/lib
    )
    find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
        PATHS /usr/include /usr/local/include
    )
    if(NOT VULKAN_LIBRARY OR NOT VULKAN_INCLUDE_DIR)
        message(FATAL_ERROR "Vulkan not found. Install vulkan-headers and vulkan-icd-loader.")
    endif()
    add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
        IMPORTED_LOCATION "${VULKAN_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_INCLUDE_DIR}"
    )
endif()

# Manual GLFW find (for systems without pkg-config)
find_library(GLFW_LIBRARY glfw glfw3
    PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/lib/x86_64-linux-gnu
)
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
    PATHS /usr/include /usr/local/include
)
if(NOT GLFW_LIBRARY OR NOT GLFW_INCLUDE_DIR)
    message(FATAL_ERROR "GLFW not found. Install glfw or glfw-wayland.")
endif()

# Shader compiler
find_program(GLSLANG glslangValidator PATHS /usr/bin /usr/local/bin)
if(NOT GLSLANG)
    find_program(GLSLANG glslc PATHS /usr/bin /usr/local/bin)
endif()
if(NOT GLSLANG)
    message(FATAL_ERROR "No shader compiler found. Install glslang or shaderc.")
endif()

# Compile shaders
set(VERT_SHADER "${CMAKE_SOURCE_DIR}/shader.vert")
set(FRAG_SHADER "${CMAKE_SOURCE_DIR}/shader.frag")
set(VERT_SPV "${CMAKE_BINARY_DIR}/vert.spv")
set(FRAG_SPV "${CMAKE_BINARY_DIR}/frag.spv")

if(GLSLANG MATCHES "glslangValidator")
    set(SHADER_COMPILE_FLAG "-V")
else()
    set(SHADER_COMPILE_FLAG "")
endif()

add_custom_command(
    OUTPUT ${VERT_SPV}
    COMMAND ${GLSLANG} ${SHADER_COMPILE_FLAG} ${VERT_SHADER} -o ${VERT_SPV}
    DEPENDS ${VERT_SHADER}
)
add_custom_command(
    OUTPUT ${FRAG_SPV}
    COMMAND ${GLSLANG} ${SHADER_COMPILE_FLAG} ${FRAG_SHADER} -o ${FRAG_SPV}
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

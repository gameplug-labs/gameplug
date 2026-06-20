cmake_minimum_required(VERSION 3.20)
project(GamePlug CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
endif()

include_directories(${PROJECT_SOURCE_DIR}/external/spdlog/include)

# Architecture Detection for bin folder
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BIN_ARCH "x64")
else()
    set(BIN_ARCH "x32")
endif()

set(BIN_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}")
file(MAKE_DIRECTORY ${BIN_OUTPUT_DIR})

# External dependencies
add_subdirectory(external/volk)
add_subdirectory(external/minhook)

# Framework and Vulkan Subprojects
add_subdirectory(src)



add_subdirectory(examples)

# x32-only Subprojects

# x64-only Subprojects (REFramework, DX-specific)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # REFramework Configuration
    set(REF_BUILD_RE4_SDK ON CACHE BOOL "" FORCE)
    set(REF_BUILD_FRAMEWORK OFF CACHE BOOL "" FORCE)
    set(REF_BUILD_DEPENDENCIES ON CACHE BOOL "" FORCE)

endif()


# Copy the manifest to the build dir
configure_file(
    ${PROJECT_SOURCE_DIR}/src/vk/VK_LAYER_GAMEPLUG.json
    ${CMAKE_CURRENT_BINARY_DIR}/VK_LAYER_GAMEPLUG.json
    COPYONLY
)




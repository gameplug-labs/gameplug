cmake_minimum_required(VERSION 3.20)

add_library(dinput8_d3d9 SHARED
    d3d9.cpp
    d3d9.def
)

target_include_directories(dinput8_d3d9 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d9
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)

target_compile_definitions(dinput8_d3d9 PRIVATE 
)

if(MSVC)
    target_compile_options(dinput8_d3d9 PRIVATE /utf-8 /FS)
endif()

# Standard Windows libraries + Framework
target_link_libraries(dinput8_d3d9 PRIVATE
    shlwapi
    framework_d3d9
    minhook
    d3d9
    d3dcompiler
    dxguid
)

if(MSVC)
    set_target_properties(dinput8_d3d9 PROPERTIES 
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
    )
else()
    set_target_properties(dinput8_d3d9 PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
    )
endif()

# Deployment
if(BIN_OUTPUT_DIR)
    add_custom_command(TARGET dinput8_d3d9 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/d3d9"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
                "$<TARGET_FILE:dinput8_d3d9>" 
                "${BIN_OUTPUT_DIR}/d3d9/dinput8.dll"
        COMMENT "Copying D3D9 proxy to d3d9/dinput8.dll"
    )
endif()

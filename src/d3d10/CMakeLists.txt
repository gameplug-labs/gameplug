cmake_minimum_required(VERSION 3.20)

add_library(dinput8_d3d10 SHARED
    dinput.cpp
    dinput.def
)

target_include_directories(dinput8_d3d10 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d10
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)

# Standard Windows libraries + Framework
target_link_libraries(dinput8_d3d10 PRIVATE
    shlwapi
    framework_d3d10
    minhook
    d3d10
    dxgi
    d3dcompiler
    delayimp.lib
)

if(MSVC)
    set_target_properties(dinput8_d3d10 PROPERTIES 
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
        LINK_FLAGS "/DELAYLOAD:d3d10.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
    )
else()
    set_target_properties(dinput8_d3d10 PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
    )
endif()

# Post-build copy to root bin folder
add_custom_command(TARGET dinput8_d3d10 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d10"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:dinput8_d3d10>"
            "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d10/$<TARGET_FILE_NAME:dinput8_d3d10>"
    COMMENT "Copying dinput8.dll proxy to bin/d3d10 folder"
)

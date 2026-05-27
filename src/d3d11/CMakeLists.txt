cmake_minimum_required(VERSION 3.20)

add_library(dinput8_d3d11 SHARED
    dinput.cpp
    dinput.def
)

target_include_directories(dinput8_d3d11 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d11
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)

# Standard Windows libraries + Framework
target_link_libraries(dinput8_d3d11 PRIVATE
    shlwapi
    framework_d3d11
    minhook
    d3d11
    dxgi
    d3dcompiler
    delayimp.lib
)

if(MSVC)
    set_target_properties(dinput8_d3d11 PROPERTIES 
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
        LINK_FLAGS "/DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
    )
else()
    set_target_properties(dinput8_d3d11 PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
    )
endif()

# Post-build copy to root bin folder
add_custom_command(TARGET dinput8_d3d11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:dinput8_d3d11>"
            "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11/$<TARGET_FILE_NAME:dinput8_d3d11>"
    COMMENT "Copying dinput8.dll proxy to bin/d3d11 folder"
)

# --- Skyrim AE variant: same DLL but built from framework_d3d11_skyrim_ae (SKYRIM_AE defined) ---
add_library(dinput8_d3d11_skyrim_ae SHARED
    dinput.cpp
    dinput_skyrim_ae.def
)

target_include_directories(dinput8_d3d11_skyrim_ae PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d11
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)

target_link_libraries(dinput8_d3d11_skyrim_ae PRIVATE
    shlwapi
    framework_d3d11_skyrim_ae
    minhook
    d3d11
    dxgi
    d3dcompiler
    delayimp.lib
)

if(MSVC)
    set_target_properties(dinput8_d3d11_skyrim_ae PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
        LINK_FLAGS "/DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
    )
else()
    set_target_properties(dinput8_d3d11_skyrim_ae PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
    )
endif()

# Post-build copy to bin/d3d11_skyrim_ae folder
add_custom_command(TARGET dinput8_d3d11_skyrim_ae POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11_skyrim_ae"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:dinput8_d3d11_skyrim_ae>"
            "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11_skyrim_ae/$<TARGET_FILE_NAME:dinput8_d3d11_skyrim_ae>"
    COMMENT "Copying dinput8.dll (Skyrim AE) proxy to bin/d3d11_skyrim_ae folder"
)

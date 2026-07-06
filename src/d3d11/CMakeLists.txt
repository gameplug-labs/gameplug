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
    target_compile_options(dinput8_d3d11 PRIVATE /utf-8 /FS)
    set_target_properties(dinput8_d3d11 PROPERTIES 
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/dinput.def\" /DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
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

# Version DLL target
add_library(version_d3d11 SHARED
    version.cpp
    version.def
)

target_include_directories(version_d3d11 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d11
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)

target_link_libraries(version_d3d11 PRIVATE
    shlwapi
    framework_d3d11
    minhook
    d3d11
    dxgi
    d3dcompiler
    delayimp.lib
)

if(MSVC)
    target_compile_options(version_d3d11 PRIVATE /utf-8 /FS)
    set_target_properties(version_d3d11 PROPERTIES 
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "version"
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/version.def\" /DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
    )
else()
    set_target_properties(version_d3d11 PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "version"
    )
endif()

# Post-build copy to root bin folder
add_custom_command(TARGET version_d3d11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:version_d3d11>"
            "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11/$<TARGET_FILE_NAME:version_d3d11>"
    COMMENT "Copying version.dll proxy to bin/d3d11 folder"
)


# --- Fallout 4 variant: same DLL but built from framework_d3d11_fallout4 (FALLOUT4 defined) ---
add_library(version_d3d11_fallout4 SHARED
    version.cpp
    version_fallout4.def
)

target_include_directories(version_d3d11_fallout4 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d11
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)

target_link_libraries(version_d3d11_fallout4 PRIVATE
    shlwapi
    framework_d3d11_fallout4
    minhook
    d3d11
    dxgi
    d3dcompiler
    delayimp.lib
)

if(MSVC)
    target_compile_options(version_d3d11_fallout4 PRIVATE /utf-8 /FS)
    set_target_properties(version_d3d11_fallout4 PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "version"
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/version_fallout4.def\" /DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
    )
else()
    set_target_properties(version_d3d11_fallout4 PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "version"
    )
endif()

# Post-build copy to bin/d3d11_fallout4 folder
add_custom_command(TARGET version_d3d11_fallout4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11_fallout4"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:version_d3d11_fallout4>"
            "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11_fallout4/$<TARGET_FILE_NAME:version_d3d11_fallout4>"
    COMMENT "Copying version.dll (Fallout 4) proxy to bin/d3d11_fallout4 folder"
)


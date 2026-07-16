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

# Version DLL target
add_library(version_d3d11 SHARED
    ../shared/version.cpp
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
        LINK_FLAGS "/DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
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

# WinMM DLL proxy
add_library(winmm_d3d11 SHARED ../shared/winmm.cpp)
target_include_directories(winmm_d3d11 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d11
    ${PROJECT_SOURCE_DIR}/src/shared
)
target_link_libraries(winmm_d3d11 PRIVATE framework_d3d11 minhook d3d11 dxgi d3dcompiler delayimp.lib)
if(MSVC)
    target_compile_options(winmm_d3d11 PRIVATE /utf-8 /FS)
    set_target_properties(winmm_d3d11 PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "winmm"
        LINK_FLAGS "/DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll")
else()
    set_target_properties(winmm_d3d11 PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "winmm")
endif()
add_custom_command(TARGET winmm_d3d11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:winmm_d3d11>"
            "${PROJECT_SOURCE_DIR}/bin/${BIN_ARCH}/d3d11/$<TARGET_FILE_NAME:winmm_d3d11>"
    COMMENT "Copying winmm.dll proxy to bin/d3d11 folder"
)


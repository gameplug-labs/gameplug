add_library(dinput8 SHARED
    dinput.cpp
    dinput.def
)

target_include_directories(dinput8 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d12
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
    ${PROJECT_SOURCE_DIR}/external/REFramework/shared
    ${PROJECT_SOURCE_DIR}/external/REFramework/include
    ${PROJECT_SOURCE_DIR}/external/safetyhook/include
)

target_compile_definitions(dinput8 PRIVATE 
    GamePlug_DIRECTX
    RE4 REENGINE_PACKED REENGINE_AT
)

# Standard Windows libraries + Framework
target_link_libraries(dinput8 PRIVATE
    shlwapi
    framework
    minhook
    d3d11
    d3d12
    dxgi
    d3dcompiler
    delayimp.lib
    RE4SDK
    utility
)

if(MSVC)
    set_target_properties(dinput8 PROPERTIES 
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
        LINK_FLAGS "/DELAYLOAD:d3d11.dll /DELAYLOAD:d3d12.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
    )
else()
    set_target_properties(dinput8 PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
    )
endif()

# Post-build copy to root bin folder
add_custom_command(TARGET dinput8 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:dinput8>"
            "${BIN_OUTPUT_DIR}/$<TARGET_FILE_NAME:dinput8>"
    COMMENT "Copying dinput8.dll proxy to bin folder"
)

cmake_minimum_required(VERSION 3.20)

# --- D3D9 Proxy Target ---
add_library(d3d9_proxy_dll SHARED
    d3d9_proxy.cpp
    d3d9.def
    ${PROJECT_SOURCE_DIR}/src/shared/logger.cpp
    ${PROJECT_SOURCE_DIR}/src/shared/config.cpp
    ${PROJECT_SOURCE_DIR}/src/shared/plugin_manager.cpp
    ${PROJECT_SOURCE_DIR}/src/shared/imgui_overlay_shared.cpp
    dx_overlay.cpp
    upscaler_manager.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx9.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_win32.cpp
)

target_include_directories(d3d9_proxy_dll PRIVATE 
    ${PROJECT_SOURCE_DIR}/src/d3d9
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/spdlog/include
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
    ${PROJECT_SOURCE_DIR}/external/minhook/include
)

set_target_properties(d3d9_proxy_dll PROPERTIES OUTPUT_NAME "dinput8")
target_compile_definitions(d3d9_proxy_dll PRIVATE GamePlug_INTERNAL_LOGGER FRAMEWORK_EXPORTS)

if(MSVC)
    target_compile_options(d3d9_proxy_dll PRIVATE /utf-8 /FS)
endif()

target_link_libraries(d3d9_proxy_dll PRIVATE d3d9.lib d3dcompiler.lib dxguid.lib minhook)

# Deployment
if(BIN_OUTPUT_DIR)
    add_custom_command(TARGET d3d9_proxy_dll POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/d3d9"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
                "$<TARGET_FILE:d3d9_proxy_dll>" 
                "${BIN_OUTPUT_DIR}/d3d9/dinput8.dll"
        COMMENT "Copying D3D9 proxy to d3d9/dinput8.dll"
    )
endif()

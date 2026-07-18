set(SHARED_SOURCES
    shared/logger.cpp
    shared/config.cpp
    shared/plugin_manager.cpp
    shared/imgui_overlay_shared.cpp
    shared/win32_hooks.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_demo.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_win32.cpp
)

# --- Vulkan Framework ---
add_library(framework_vk STATIC
    ${SHARED_SOURCES}
    vk/framework.cpp
    vk/overlay.cpp
    vk/image_tracker.cpp
    vk/dispatch.cpp
    vk/jitter_helper.cpp
    ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/vk/upscaler_manager.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_vulkan.cpp
)

target_compile_definitions(framework_vk PRIVATE 
    FRAMEWORK_EXPORTS
    GAMEPLUG_VULKAN
    GAMEPLUG_SKYRIM
)
target_include_directories(framework_vk PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/vk
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/vk
    ${PROJECT_SOURCE_DIR}/external/volk
    ${PROJECT_SOURCE_DIR}/external/spdlog/include
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)
target_compile_definitions(framework_vk PUBLIC 
    VK_NO_PROTOTYPES
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    "IMGUI_USER_CONFIG=\"GAMEPLUG_imgui_config.h\""
)
target_link_libraries(framework_vk PUBLIC volk minhook)

if(MSVC)
    target_compile_options(framework_vk PRIVATE /utf-8 /FS)
endif()
add_subdirectory(vk)

# --- D3D11 Framework ---
add_library(framework_d3d11 STATIC
    ${SHARED_SOURCES}
    d3d11/framework.cpp
    d3d11/overlay.cpp
    d3d11/hooks.cpp
    d3d11/hooks_d3d11.cpp
    d3d11/hooks_dxgi.cpp
    ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/d3d11/upscaler_manager.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx11.cpp
)

target_compile_definitions(framework_d3d11 PRIVATE 
    FRAMEWORK_EXPORTS
)
target_include_directories(framework_d3d11 PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/d3d11
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/d3d11
    ${PROJECT_SOURCE_DIR}/external/spdlog/include
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
)
target_compile_definitions(framework_d3d11 PUBLIC 
    "IMGUI_USER_CONFIG=\"GAMEPLUG_imgui_config.h\""
)
target_link_libraries(framework_d3d11 PUBLIC 
    minhook
    d3d11
    dxgi
    d3dcompiler
    delayimp.lib
)

if(MSVC)
    target_compile_options(framework_d3d11 PRIVATE /utf-8 /FS)
    set_target_properties(framework_d3d11 PROPERTIES LINK_FLAGS 
        "/DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
    )
endif()
add_subdirectory(d3d11)

# --- DirectX Framework (x64 only) ---
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # --- D3D12 Framework ---
    add_library(framework_d3d12 STATIC
        ${SHARED_SOURCES}
        d3d12/framework.cpp
        d3d12/overlay.cpp
        d3d12/hooks.cpp
        d3d12/hooks_d3d12.cpp
        d3d12/hooks_dxgi.cpp
        ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx12.cpp
    )

    target_compile_definitions(framework_d3d12 PRIVATE 
        FRAMEWORK_EXPORTS
    )
    target_include_directories(framework_d3d12 PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/d3d12
        ${PROJECT_SOURCE_DIR}/src/shared
        ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/d3d12
        ${PROJECT_SOURCE_DIR}/external/spdlog/include
        ${PROJECT_SOURCE_DIR}/external/imgui
        ${PROJECT_SOURCE_DIR}/external/imgui/backends
    )
    target_compile_definitions(framework_d3d12 PUBLIC 
        "IMGUI_USER_CONFIG=\"GAMEPLUG_imgui_config.h\""
    )
    target_link_libraries(framework_d3d12 PUBLIC 
        minhook
        d3d12
        dxgi
        d3dcompiler
        delayimp.lib
    )

    if(MSVC)
        target_compile_options(framework_d3d12 PRIVATE /utf-8 /FS)
        set_target_properties(framework_d3d12 PROPERTIES LINK_FLAGS 
            "/DELAYLOAD:d3d12.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
        )
    endif()

    add_subdirectory(d3d12)
endif()

# --- Post-build naming ---
set_target_properties(framework_vk PROPERTIES OUTPUT_NAME "framework_vk")
set_target_properties(framework_d3d11 PROPERTIES OUTPUT_NAME "framework_d3d11")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(framework_d3d12 PROPERTIES OUTPUT_NAME "framework_d3d12")
endif()


# --- D3D9 / D3D10 Support (x32 only) ---
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # --- D3D10 Framework ---
    add_library(framework_d3d10 STATIC
        ${SHARED_SOURCES}
        d3d10/framework.cpp
        d3d10/overlay.cpp
        d3d10/hooks.cpp
        d3d10/hooks_d3d10.cpp
        d3d10/hooks_dxgi.cpp
        ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx10.cpp
    )

    set_target_properties(framework_d3d10 PROPERTIES OUTPUT_NAME "framework_d3d10")

    target_compile_definitions(framework_d3d10 PRIVATE 
        FRAMEWORK_EXPORTS
    )
    target_include_directories(framework_d3d10 PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/d3d10
        ${PROJECT_SOURCE_DIR}/src/shared
        ${PROJECT_SOURCE_DIR}/external/spdlog/include
        ${PROJECT_SOURCE_DIR}/external/imgui
        ${PROJECT_SOURCE_DIR}/external/imgui/backends
    )
    target_compile_definitions(framework_d3d10 PUBLIC 
        "IMGUI_USER_CONFIG=\"GAMEPLUG_imgui_config.h\""
    )
    target_link_libraries(framework_d3d10 PUBLIC 
        minhook
        d3d10
        dxgi
        d3dcompiler
        delayimp.lib
    )

    if(MSVC)
        target_compile_options(framework_d3d10 PRIVATE /utf-8 /FS)
        set_target_properties(framework_d3d10 PROPERTIES LINK_FLAGS 
            "/DELAYLOAD:d3d10.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
        )
    endif()

    add_subdirectory(d3d10)

    # --- D3D9 Framework ---
    add_library(framework_d3d9 STATIC
        ${SHARED_SOURCES}
        d3d9/framework.cpp
        d3d9/d3d9_proxy.cpp
        d3d9/d3d9_proxy_d3d9.cpp
        d3d9/d3d9_proxy_device.cpp
        d3d9/d3d9_proxy_surface.cpp
        d3d9/d3d9_proxy_swapchain.cpp
        # d3d9/d3d9_proxy_texture.cpp
        # d3d9/texture_replacer.cpp
        d3d9/dx_overlay.cpp
        ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/d3d9/upscaler_manager.cpp
        ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx9.cpp
    )

    set_target_properties(framework_d3d9 PROPERTIES OUTPUT_NAME "framework_d3d9")

    target_compile_definitions(framework_d3d9 PRIVATE 
        FRAMEWORK_EXPORTS
        GAMEPLUG_WIN32
        GamePlug_INTERNAL_LOGGER
    )
    target_include_directories(framework_d3d9 PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/d3d9
        ${PROJECT_SOURCE_DIR}/src/shared
        ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/d3d9
        ${PROJECT_SOURCE_DIR}/external/spdlog/include
        ${PROJECT_SOURCE_DIR}/external/imgui
        ${PROJECT_SOURCE_DIR}/external/imgui/backends
        ${PROJECT_SOURCE_DIR}/external/minhook/include
    )
    target_compile_definitions(framework_d3d9 PUBLIC 
        "IMGUI_USER_CONFIG=\"GAMEPLUG_imgui_config.h\""
    )
    target_link_libraries(framework_d3d9 PUBLIC 
        minhook
        d3d9
        d3dcompiler
        dxguid
    )

    if(MSVC)
        target_compile_options(framework_d3d9 PRIVATE /utf-8 /FS)
    endif()

    # --- Skyrim Framework ---
    add_library(framework_skyrim STATIC
        ${SHARED_SOURCES}
        d3d9/framework.cpp
        d3d9/d3d9_proxy.cpp
        d3d9/d3d9_proxy_d3d9.cpp
        d3d9/d3d9_proxy_device.cpp
        d3d9/d3d9_proxy_surface.cpp
        d3d9/d3d9_proxy_swapchain.cpp
        # d3d9/d3d9_proxy_texture.cpp
        # d3d9/texture_replacer.cpp
        d3d9/dx_overlay.cpp
        ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/d3d9/upscaler_manager.cpp
        ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx9.cpp
    )

    set_target_properties(framework_skyrim PROPERTIES OUTPUT_NAME "framework_skyrim")

    target_compile_definitions(framework_skyrim PRIVATE 
        FRAMEWORK_EXPORTS
        GamePlug_INTERNAL_LOGGER
        GAMEPLUG_SKYRIM
    )
    target_include_directories(framework_skyrim PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/d3d9
        ${PROJECT_SOURCE_DIR}/src/shared
        ${PROJECT_SOURCE_DIR}/sub_projects/upscaler-manager/d3d9
        ${PROJECT_SOURCE_DIR}/external/spdlog/include
        ${PROJECT_SOURCE_DIR}/external/imgui
        ${PROJECT_SOURCE_DIR}/external/imgui/backends
        ${PROJECT_SOURCE_DIR}/external/minhook/include
    )
    target_compile_definitions(framework_skyrim PUBLIC 
        "IMGUI_USER_CONFIG=\"GAMEPLUG_imgui_config.h\""
    )
    target_link_libraries(framework_skyrim PUBLIC 
        minhook
        d3d9
        d3dcompiler
        dxguid
    )

    if(MSVC)
        target_compile_options(framework_skyrim PRIVATE /utf-8 /FS)
    endif()

    add_subdirectory(d3d9)
endif()



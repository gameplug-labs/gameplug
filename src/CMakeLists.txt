set(SHARED_SOURCES
    shared/logger.cpp
    shared/config.cpp
    shared/plugin_manager.cpp
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
    ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_vulkan.cpp
)

target_compile_definitions(framework_vk PRIVATE 
    FRAMEWORK_EXPORTS
    GAMEPLUG_VULKAN
)
target_include_directories(framework_vk PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/vk
    ${PROJECT_SOURCE_DIR}/src/shared
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
    target_compile_options(framework_vk PRIVATE /utf-8)
endif()

# --- DirectX Framework (x64 only) ---
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_library(framework STATIC
        ${SHARED_SOURCES}
        d3d12/framework.cpp
        d3d12/overlay.cpp
        d3d12/hooks.cpp
        ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx11.cpp
        ${PROJECT_SOURCE_DIR}/external/imgui/backends/imgui_impl_dx12.cpp
    )

    target_compile_definitions(framework PRIVATE 
        FRAMEWORK_EXPORTS
        GAMEPLUG_DIRECTX
    )
    target_include_directories(framework PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/d3d12
        ${PROJECT_SOURCE_DIR}/src/shared
        ${PROJECT_SOURCE_DIR}/external/spdlog/include
        ${PROJECT_SOURCE_DIR}/external/imgui
        ${PROJECT_SOURCE_DIR}/external/imgui/backends
        ${PROJECT_SOURCE_DIR}/external/REFramework/shared
        ${PROJECT_SOURCE_DIR}/external/REFramework/include
        ${PROJECT_SOURCE_DIR}/external/safetyhook/include
    )
    target_compile_definitions(framework PUBLIC 
        "IMGUI_USER_CONFIG=\"GAMEPLUG_imgui_config.h\""
        RE4 REENGINE_PACKED REENGINE_AT
    )
    target_link_libraries(framework PUBLIC 
        minhook
        d3d11
        d3d12
        dxgi
        d3dcompiler
        delayimp.lib
    )

    if(MSVC)
        target_compile_options(framework PRIVATE /utf-8)
        set_target_properties(framework PROPERTIES LINK_FLAGS 
            "/DELAYLOAD:d3d11.dll /DELAYLOAD:d3d12.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll"
        )
    endif()

    add_subdirectory(d3d12)
endif()

# --- Post-build naming ---
set_target_properties(framework_vk PROPERTIES OUTPUT_NAME "framework_vk")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(framework PROPERTIES OUTPUT_NAME "framework")
endif()


# --- D3D9 Support (x32 only) ---
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory(d3d9)
endif()

# --- Vulkan Loader ---
add_executable(GAMEPLUG 
    vk/main.cpp
)

target_link_libraries(GAMEPLUG 
    PRIVATE 
    kernel32.lib
    shlwapi.lib
)

if(MSVC)
    target_compile_options(GAMEPLUG PRIVATE /FS)
endif()

set_target_properties(GAMEPLUG PROPERTIES OUTPUT_NAME "GAMEPLUG")

add_custom_command(TARGET GAMEPLUG POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:GAMEPLUG>"
            "${BIN_OUTPUT_DIR}/$<TARGET_FILE_NAME:GAMEPLUG>"
    COMMENT "Copying loader EXE to bin folder"
)



# --- Vulkan Layer ---
add_library(vklayer SHARED
    vk/vk_layer_exports.cpp
    vk/vklayer.def
)

target_include_directories(vklayer PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/volk
)

target_compile_definitions(vklayer PRIVATE 
    GAMEPLUG_VULKAN
    FRAMEWORK_EXPORTS
)

if(MSVC)
    target_compile_options(vklayer PRIVATE /utf-8)
endif()

target_link_libraries(vklayer 
    PRIVATE 
    framework_vk
    volk
)

# Post-build copy to root bin folder
add_custom_command(TARGET vklayer POST_BUILD
    # 1. Copy the DLL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:vklayer>"
            "${BIN_OUTPUT_DIR}/$<TARGET_FILE_NAME:vklayer>"
    
    # 2. Copy the manifest file (VK_LAYER_GAMEPLUG.json)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/VK_LAYER_GAMEPLUG.json"
            "${BIN_OUTPUT_DIR}/VK_LAYER_GAMEPLUG.json"

    COMMENT "Copying layer DLL and manifest to bin folder"
)

# Export the layer functions
if(WIN32)
    set_target_properties(vklayer PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
endif()




# Examples for GamePlug

set(IMGUI_SOURCES
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
)

# 1. Sample Plugin
add_library(sample_plugin SHARED 
    sample/sample_plugin.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(sample_plugin PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/imgui
)
set_target_properties(sample_plugin PROPERTIES OUTPUT_NAME "sample" PREFIX "")

# Copy sample plugin to bin folder
add_custom_command(TARGET sample_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/gameplug/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:sample_plugin>" 
            "${BIN_OUTPUT_DIR}/gameplug/plugins/sample.dll"
    COMMENT "Copying sample_plugin to bin/${BIN_ARCH}/gameplug/plugins"
)

# 2. Extra Plugin
add_library(extra_plugin SHARED 
    extra/extra_plugin.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(extra_plugin PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/imgui
)
set_target_properties(extra_plugin PROPERTIES OUTPUT_NAME "extra" PREFIX "")

# Copy extra plugin to bin folder
add_custom_command(TARGET extra_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/gameplug/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:extra_plugin>" 
            "${BIN_OUTPUT_DIR}/gameplug/plugins/extra.dll"
    COMMENT "Copying extra_plugin to bin/${BIN_ARCH}/gameplug/plugins"
)
# 3. Minimal Plugin
add_library(minimal_plugin SHARED 
    minimal_plugin.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(minimal_plugin PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/imgui
)
set_target_properties(minimal_plugin PROPERTIES OUTPUT_NAME "minimal" PREFIX "")

# Copy minimal plugin to bin folder
add_custom_command(TARGET minimal_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/gameplug/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:minimal_plugin>" 
            "${BIN_OUTPUT_DIR}/gameplug/plugins/minimal.dll"
    COMMENT "Copying minimal_plugin to bin/${BIN_ARCH}/gameplug/plugins"
)

# 4. Graphic API Plugin
add_library(vk_graphic_api_plugin SHARED 
    vk_graphic_api_plugin.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(vk_graphic_api_plugin PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/volk
)
set_target_properties(vk_graphic_api_plugin PROPERTIES OUTPUT_NAME "vk_graphic_api" PREFIX "")
target_link_libraries(vk_graphic_api_plugin PRIVATE volk)

# Copy graphic_api plugin to bin folder
add_custom_command(TARGET vk_graphic_api_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/gameplug/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:vk_graphic_api_plugin>" 
            "${BIN_OUTPUT_DIR}/gameplug/plugins/vk_graphic_api.dll"
    COMMENT "Copying vk_graphic_api_plugin to bin/${BIN_ARCH}/gameplug/plugins"
)

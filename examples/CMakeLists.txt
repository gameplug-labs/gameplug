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
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/GamePlug/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:sample_plugin>" 
            "${BIN_OUTPUT_DIR}/GamePlug/plugins/sample.dll"
    COMMENT "Copying sample_plugin to bin/${BIN_ARCH}/GamePlug/plugins"
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
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/GamePlug/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:extra_plugin>" 
            "${BIN_OUTPUT_DIR}/GamePlug/plugins/extra.dll"
    COMMENT "Copying extra_plugin to bin/${BIN_ARCH}/GamePlug/plugins"
)

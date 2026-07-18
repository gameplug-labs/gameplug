# --- Vulkan MinHook Proxies ---
set(VK_HOOK_SOURCES
    vk_layer_exports.cpp
    vk_layer_lifecycle.cpp
    vk_layer_resources.cpp
    vk_layer_commands.cpp
)

add_library(dinput8_vk SHARED dinput8.cpp ${VK_HOOK_SOURCES})
target_include_directories(dinput8_vk PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/vk
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/volk
)
target_compile_definitions(dinput8_vk PRIVATE 
    GAMEPLUG_VULKAN
    FRAMEWORK_EXPORTS
)
target_link_libraries(dinput8_vk PRIVATE framework_vk volk)
set_target_properties(dinput8_vk PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        OUTPUT_NAME "dinput8"
)

add_custom_command(TARGET dinput8_vk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:dinput8_vk>"
            "${BIN_OUTPUT_DIR}/$<TARGET_FILE_NAME:dinput8_vk>"
    COMMENT "Copying dinput8 proxy to bin folder"
)

add_library(version_vk SHARED version.cpp ${VK_HOOK_SOURCES})
target_include_directories(version_vk PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/vk
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/volk
)
target_compile_definitions(version_vk PRIVATE 
    GAMEPLUG_VULKAN
    FRAMEWORK_EXPORTS
)
target_link_libraries(version_vk PRIVATE framework_vk volk)
set_target_properties(version_vk PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "version")

add_custom_command(TARGET version_vk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:version_vk>"
            "${BIN_OUTPUT_DIR}/$<TARGET_FILE_NAME:version_vk>"
    COMMENT "Copying version proxy to bin folder"
)

add_library(winmm_vk SHARED winmm.cpp ${VK_HOOK_SOURCES})
target_include_directories(winmm_vk PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/vk
    ${PROJECT_SOURCE_DIR}/src/shared
    ${PROJECT_SOURCE_DIR}/external/volk
)
target_compile_definitions(winmm_vk PRIVATE 
    GAMEPLUG_VULKAN
    FRAMEWORK_EXPORTS
)
target_link_libraries(winmm_vk PRIVATE framework_vk volk)
set_target_properties(winmm_vk PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "winmm")

add_custom_command(TARGET winmm_vk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:winmm_vk>"
            "${BIN_OUTPUT_DIR}/$<TARGET_FILE_NAME:winmm_vk>"
    COMMENT "Copying winmm proxy to bin folder"
)

if(MSVC)
    target_compile_options(version_vk PRIVATE /utf-8 /FS)
    target_compile_options(dinput8_vk PRIVATE /utf-8 /FS)
    target_compile_options(winmm_vk PRIVATE /utf-8 /FS)
endif()
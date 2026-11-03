add_library(xenia-gpu-vulkan STATIC)
xe_platform_sources(xenia-gpu-vulkan ${CMAKE_CURRENT_SOURCE_DIR})
xe_shader_rules_spirv(xenia-gpu-vulkan ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
target_include_directories(xenia-gpu-vulkan PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
)
if(WIN32 AND DEFINED ENV{VULKAN_SDK})
  target_include_directories(xenia-gpu-vulkan PRIVATE "$ENV{VULKAN_SDK}/Include")
endif()
target_link_libraries(xenia-gpu-vulkan PUBLIC
  fmt xenia-spirv-writer retro-common xenia-base xenia-gpu xenia-ui xenia-ui-vulkan
)
xe_target_defaults(xenia-gpu-vulkan)

add_library(xenia-ui-vulkan STATIC)
xe_platform_sources(xenia-ui-vulkan ${CMAKE_CURRENT_SOURCE_DIR})
xe_platform_sources(xenia-ui-vulkan ${CMAKE_CURRENT_SOURCE_DIR}/functions)
target_include_directories(xenia-ui-vulkan PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
)
target_link_libraries(xenia-ui-vulkan PUBLIC xenia-base xenia-ui)
xe_target_defaults(xenia-ui-vulkan)

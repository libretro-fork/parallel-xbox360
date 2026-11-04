add_library(xenia-ui-d3d12 STATIC)
xe_platform_sources(xenia-ui-d3d12 ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xenia-ui-d3d12 PUBLIC xenia-base xenia-ui)
xe_target_defaults(xenia-ui-d3d12)

add_library(xenia-ui STATIC)
xe_platform_sources(xenia-ui ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xenia-ui PUBLIC xenia-base)
xe_target_defaults(xenia-ui)

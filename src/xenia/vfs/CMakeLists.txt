add_library(xenia-vfs STATIC)
xe_platform_sources(xenia-vfs ${CMAKE_CURRENT_SOURCE_DIR} RECURSIVE)
target_link_libraries(xenia-vfs PUBLIC xenia-base zarchive)
xe_target_defaults(xenia-vfs)

if(XENIA_BUILD_TESTS)
  add_subdirectory(testing)
endif()

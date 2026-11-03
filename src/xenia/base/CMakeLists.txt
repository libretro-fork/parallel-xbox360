add_library(xenia-base STATIC)
xe_platform_sources(xenia-base ${CMAKE_CURRENT_SOURCE_DIR})
# Exclude console_app_main and main_init files
file(GLOB _base_exclude
  "${CMAKE_CURRENT_SOURCE_DIR}/console_app_main_*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/main_init_*.cc"
)
get_target_property(_base_srcs xenia-base SOURCES)
if(_base_exclude AND _base_srcs)
  list(REMOVE_ITEM _base_srcs ${_base_exclude})
  set_target_properties(xenia-base PROPERTIES SOURCES "${_base_srcs}")
endif()
target_link_libraries(xenia-base PUBLIC fmt retro-common)
target_link_libraries(xenia-base PRIVATE boost_context)
target_sources(xenia-base PRIVATE "${PROJECT_SOURCE_DIR}/libretro/core_decompress.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_audio_cadence.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_audio_samples.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_audio_queue.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_input_analog.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_image.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_ui_queue.c")
xe_target_defaults(xenia-base)

if(XENIA_BUILD_TESTS)
  add_subdirectory(testing)
endif()

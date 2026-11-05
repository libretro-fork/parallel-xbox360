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
target_include_directories(xenia-base PRIVATE "${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include")
target_sources(xenia-base PRIVATE "${PROJECT_SOURCE_DIR}/libretro/core_vblank.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_bitmap.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_socket_worker.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_gpu_retire.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_gpu_worklist.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_gpu_spans.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_vk_heap.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_pause_gate.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_cache_file.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_file_blocks.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_io_mailbox.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_input_mailbox.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_cache_writer.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_pipeline_order.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_job_pool.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_fiber_context.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_decompress.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_audio_cadence.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_audio_samples.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_audio_queue.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_audio_convert.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_input_analog.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_image.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_ui_queue.c")
xe_target_defaults(xenia-base)

if(XENIA_BUILD_TESTS)
  add_subdirectory(testing)
endif()

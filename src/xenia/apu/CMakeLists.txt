add_library(xenia-xma-native STATIC ${PROJECT_SOURCE_DIR}/libretro/core_xma_native.c)
set_target_properties(xenia-xma-native PROPERTIES C_STANDARD 90 C_STANDARD_REQUIRED ON)
target_include_directories(xenia-xma-native PUBLIC ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/third_party/libretro-common/include)
target_link_libraries(xenia-xma-native PUBLIC retro-common)

add_library(xenia-apu STATIC)
xe_platform_sources(xenia-apu ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(xenia-apu PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/FFmpeg
  ${PROJECT_SOURCE_DIR}/third_party/ffmpeg-xenia
)
target_link_libraries(xenia-apu PUBLIC xenia-xma-native libavcodec libavutil libavformat xenia-base)
xe_target_defaults(xenia-apu)

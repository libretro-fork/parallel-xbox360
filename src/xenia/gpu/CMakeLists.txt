# Raw SPIR-V binary writer reused as editable core source. No GLSL frontend.
add_library(xenia-spirv-writer STATIC
  spirv/Logger.cpp
  spirv/SpvBuilder.cpp
  spirv/SpvPostProcess.cpp
  spirv/InReadableOrder.cpp
  "${PROJECT_SOURCE_DIR}/libretro/core_spv_operands.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_spv_binary.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_spv_arena.c"
  "${PROJECT_SOURCE_DIR}/libretro/core_spv_size.c"
)
target_include_directories(xenia-spirv-writer PUBLIC
  "${PROJECT_SOURCE_DIR}/third_party/libretro-common/include")

add_library(xenia-gpu STATIC)
xe_platform_sources(xenia-gpu ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(xenia-gpu PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
)
target_link_libraries(xenia-gpu PUBLIC fmt snappy retro-common xenia-base xenia-ui)
target_link_libraries(xenia-gpu PRIVATE xenia-spirv-writer)
if(WIN32)
  file(GLOB _native_dxil_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/dxil/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/dxil/bytecode/*.c")
  add_library(xenia-native-dxil STATIC ${_native_dxil_sources})
  target_include_directories(xenia-native-dxil PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/dxil/bytecode")
  target_link_libraries(xenia-gpu PUBLIC dxbc xenia-native-dxil)
endif()
xe_target_defaults(xenia-gpu)

if(XENIA_BUILD_MISC)
  # Shader compiler tool
  add_executable(xenia-gpu-shader-compiler
    ${CMAKE_CURRENT_SOURCE_DIR}/shader_compiler_main.cc
  )
  if(WIN32)
    target_sources(xenia-gpu-shader-compiler PRIVATE
      ${PROJECT_SOURCE_DIR}/src/xenia/base/console_app_main_win.cc)
  else()
    target_sources(xenia-gpu-shader-compiler PRIVATE
      ${PROJECT_SOURCE_DIR}/src/xenia/base/console_app_main_posix.cc)
  endif()
  target_include_directories(xenia-gpu-shader-compiler PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
  )
  # The shader translators only need xenia-gpu, but linking it drags in
  # command_processor and graphics_system, so the kernel side comes with it.
  target_link_libraries(xenia-gpu-shader-compiler PRIVATE
    xenia-apu xenia-apu-nop xenia-base xenia-core xenia-cpu
    xenia-gpu xenia-hid xenia-hid-nop xenia-hid-portal
    xenia-kernel xenia-patcher xenia-ui xenia-ui-vulkan xenia-vfs
    aes_128 capstone fmt xenia-spirv-writer libavcodec libavutil mspack
    snappy retro-common
  )
  if(XE_TARGET_X86_64)
    target_link_libraries(xenia-gpu-shader-compiler PRIVATE xenia-cpu-backend-x64)
  endif()
  if(XE_TARGET_AARCH64)
    target_link_libraries(xenia-gpu-shader-compiler PRIVATE xenia-cpu-backend-a64)
  endif()
  xe_target_defaults(xenia-gpu-shader-compiler)
endif()

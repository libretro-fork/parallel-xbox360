# parallel_xbox360_libretro: the libretro core, a shared library linked from the
# emulator's static libraries, without the standalone app's UI.

add_library(xenia-libretro SHARED
  libretro.cpp
  core_events.c
  core_audio_cadence.c
  libretro_audio_driver.cc
  libretro_graphics_system.cc
  libretro_headless_stubs.cc
  libretro_hid.cc
  libretro_vk_presenter.cc
  libretro_gamedb.cc
  libretro_disc.cc
)

# The frame rate unlock list (libretro/data), carried inside the core and
# written to its system folder when missing
set(_xe_data_cc "${CMAKE_CURRENT_BINARY_DIR}/libretro_gamedb_data.cc")
set(_xe_data_text "// Generated from libretro/data by libretro/CMakeLists.txt\n#include <cstddef>\n")
foreach(_xe_pair "framerate_unlock:xenia_framerate_unlock.txt")
  string(REPLACE ":" ";" _xe_pair "${_xe_pair}")
  list(GET _xe_pair 0 _xe_symbol)
  list(GET _xe_pair 1 _xe_file)
  set(_xe_path "${CMAKE_CURRENT_SOURCE_DIR}/data/${_xe_file}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_xe_path}")
  file(READ "${_xe_path}" _xe_hex HEX)
  file(SIZE "${_xe_path}" _xe_size)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _xe_bytes "${_xe_hex}")
  string(APPEND _xe_data_text
    "extern const unsigned char xenia_libretro_${_xe_symbol}[] = {${_xe_bytes}0};\n"
    "extern const size_t xenia_libretro_${_xe_symbol}_size = ${_xe_size};\n")
endforeach()
file(WRITE "${_xe_data_cc}.tmp" "${_xe_data_text}")
file(COPY_FILE "${_xe_data_cc}.tmp" "${_xe_data_cc}" ONLY_IF_DIFFERENT)
target_sources(xenia-libretro PRIVATE "${_xe_data_cc}")
if(WIN32)
  target_sources(xenia-libretro PRIVATE libretro_d3d12_presenter.cc
    core_d3d12_fence.c core_d3d12_video.c)

endif()

# RetroArch's names: parallel_xbox360_libretro.so / .dll / .dylib, and
# parallel_xbox360_libretro_android.so on Android
set(_xe_libretro_name parallel_xbox360_libretro)
if(ANDROID)
  set(_xe_libretro_name parallel_xbox360_libretro_android)
endif()
set_target_properties(xenia-libretro PROPERTIES
  OUTPUT_NAME ${_xe_libretro_name}
  PREFIX ""
)

# As xe_target_defaults, without -Werror: the core's own code is not held
# to it
target_include_directories(xenia-libretro PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/third_party
  ${PROJECT_SOURCE_DIR}/third_party/Vulkan-Headers/include
)
# The core's version is the xenia-edge release it is merged from (its releases
# are named by commit), from upstream.version, updated with each merge
file(STRINGS ${PROJECT_SOURCE_DIR}/upstream.version _xe_upstream_version LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/upstream.version)

# And the commit of this repository it is built from, logged at start, so a
# tester's log says which build it came from. Taken when CMake configures, as
# every CI build does from scratch.
set(_xe_libretro_commit "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    RESULT_VARIABLE _xe_commit_result
    OUTPUT_VARIABLE _xe_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_xe_commit_result EQUAL 0 AND _xe_commit)
    set(_xe_libretro_commit "${_xe_commit}")
  endif()
endif()

target_compile_definitions(xenia-libretro PRIVATE
  XENIA_LIBRETRO_VERSION="${_xe_upstream_version}"
  XENIA_LIBRETRO_COMMIT="${_xe_libretro_commit}"
  XBYAK_NO_OP_NAMES
  XBYAK_ENABLE_OMITTED_OPERAND
  XENIA_LIBRETRO=1
)

set(_xe_libretro_libs
  xenia-apu
  xenia-apu-nop
  xenia-base
  xenia-core
  xenia-cpu
  xenia-gpu
  xenia-gpu-null
  xenia-gpu-vulkan
  xenia-hid
  xenia-hid-nop
  xenia-kernel
  xenia-patcher
  xenia-ui
  xenia-ui-vulkan
  xenia-vfs
  aes_128
  capstone
  fmt
  xenia-spirv-writer
  imgui
  libavcodec
  libavutil
  mspack
  snappy
  xxhash
)
if(XE_TARGET_X86_64)
  list(APPEND _xe_libretro_libs xenia-cpu-backend-x64)
elseif(XE_TARGET_AARCH64)
  list(APPEND _xe_libretro_libs xenia-cpu-backend-a64)
endif()
if(WIN32)
  list(APPEND _xe_libretro_libs xenia-gpu-d3d12 xenia-ui-d3d12)
endif()

# On Linux the static libraries' circular references are resolved by the
# link group the top-level CMakeLists.txt wraps every link in
target_link_libraries(xenia-libretro PRIVATE ${_xe_libretro_libs})

if(WIN32)
  target_link_libraries(xenia-libretro PRIVATE d3d12 dxgi)
elseif(ANDROID)
  target_link_libraries(xenia-libretro PRIVATE android log)
  # FFmpeg's assembly reaches its tables PC-relative, which a shared object
  # allows only for symbols bound inside it
  target_link_options(xenia-libretro PRIVATE -Wl,-Bsymbolic)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Only the libretro API out of the core (the version script: it exported
  # some sixteen thousand symbols before), and its own symbols bound inside
  # it, so the frontend's libraries don't stand in for them
  target_link_options(xenia-libretro PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/link.T" -Wl,--exclude-libs,ALL)
  set_property(TARGET xenia-libretro APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/link.T")
endif()

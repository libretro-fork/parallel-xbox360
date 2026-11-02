# Helper functions for the xenia build.

include(CMakeParseArguments)

set(XE_PLATFORM_SUFFIXES
  _win _linux _posix _gnulinux _x11 _gtk _android _mac _amd64 _x64 _arm64
)

# xe_platform_sources(target base_path [RECURSIVE])
#
# Globs source files from base_path and adds them to target. Excludes
# *_main.cc / *_test.cc / *_demo.cc, and any file with a platform suffix
# that doesn't match the current platform. RECURSIVE descends into
# subdirectories.
function(xe_platform_sources target base_path)
  set(options RECURSIVE)
  cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})

  if(ARG_RECURSIVE)
    set(glob_mode GLOB_RECURSE)
  else()
    set(glob_mode GLOB)
  endif()

  file(${glob_mode} _all_sources
    "${base_path}/*.h"
    "${base_path}/*.cc"
    "${base_path}/*.cpp"
    "${base_path}/*.c"
    "${base_path}/*.inc"
  )

  set(_excluded)
  foreach(src ${_all_sources})
    get_filename_component(_name_we ${src} NAME_WE)
    if(_name_we MATCHES "_main$" OR _name_we MATCHES "_test$" OR _name_we MATCHES "_demo$")
      list(APPEND _excluded ${src})
      continue()
    endif()
    foreach(suffix ${XE_PLATFORM_SUFFIXES})
      if(_name_we MATCHES "${suffix}$")
        list(APPEND _excluded ${src})
        break()
      endif()
    endforeach()
  endforeach()

  set(_sources ${_all_sources})
  if(_excluded)
    list(REMOVE_ITEM _sources ${_excluded})
  endif()

  if(WIN32)
    file(${glob_mode} _plat_sources
      "${base_path}/*_win.h"
      "${base_path}/*_win.cc"
    )
  elseif(APPLE)
    file(${glob_mode} _plat_sources
      "${base_path}/*_posix.h"
      "${base_path}/*_posix.cc"
      "${base_path}/*_mac.h"
      "${base_path}/*_mac.cc"
    )
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(${glob_mode} _plat_sources
      "${base_path}/*_posix.h"
      "${base_path}/*_posix.cc"
      "${base_path}/*_linux.h"
      "${base_path}/*_linux.cc"
      "${base_path}/*_gnulinux.h"
      "${base_path}/*_gnulinux.cc"
      "${base_path}/*_x11.h"
      "${base_path}/*_x11.cc"
      "${base_path}/*_gtk.h"
      "${base_path}/*_gtk.cc"
    )
  elseif(ANDROID)
    # As premake had it for Android-*: POSIX and Linux, plus the Android files
    file(${glob_mode} _plat_sources
      "${base_path}/*_posix.h"
      "${base_path}/*_posix.cc"
      "${base_path}/*_linux.h"
      "${base_path}/*_linux.cc"
      "${base_path}/*_android.h"
      "${base_path}/*_android.cc"
    )
  endif()

  list(APPEND _sources ${_plat_sources})

  # Add back architecture-specific files
  if(XE_TARGET_X86_64)
    file(${glob_mode} _arch_sources "${base_path}/*_amd64.h" "${base_path}/*_amd64.cc"
      "${base_path}/*_x64.h" "${base_path}/*_x64.cc")
  elseif(XE_TARGET_AARCH64)
    file(${glob_mode} _arch_sources "${base_path}/*_arm64.h" "${base_path}/*_arm64.cc")
  endif()
  if(_arch_sources)
    list(APPEND _sources ${_arch_sources})
  endif()

  target_sources(${target} PRIVATE ${_sources})
endfunction()

# xe_target_defaults(target)
#
# Applies xenia-wide compile defaults to a target: project-root include
# directories and warnings-as-errors. GCC is excluded from -Werror because
# it's too noisy to keep clean across the codebase.
function(xe_target_defaults target)
  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
  )
  if(MSVC)
    target_compile_options(${target} PRIVATE /WX)
  elseif(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

# xe_windowed_app_main(target)
#
# Adds the per-platform wx entry point a WindowedApp executable needs.
# src/xenia/ui/CMakeLists.txt keeps these out of the xenia-ui library, so every
# windowed executable has to name one itself.
function(xe_windowed_app_main target)
  if(WIN32)
    set(_main windowed_app_main_win.cc)
  elseif(APPLE)
    set(_main windowed_app_main_mac.cc)
  else()
    set(_main windowed_app_main_linux.cc)
  endif()
  target_sources(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}/src/xenia/ui/${_main})
endfunction()

# xe_embed_binary_assets(target source_dir output_namespace)
#
# Embeds every file in source_dir as a static byte array in a generated
# embedded_<namespace>.{h,cc} pair. Symbol names mirror the filename with
# '-' and '.' replaced by '_'. Generated files land in the build dir and
# are added to target's sources / include path. Re-run cmake configure
# after adding/changing assets.
function(xe_embed_binary_assets target source_dir output_namespace)
  file(GLOB asset_files "${source_dir}/*")
  set(header_path "${CMAKE_CURRENT_BINARY_DIR}/embedded_${output_namespace}.h")
  set(source_path "${CMAKE_CURRENT_BINARY_DIR}/embedded_${output_namespace}.cc")
  set(ns "xe::ui::embedded_${output_namespace}")

  # Buffer to a single string; unquoted expansion to file(WRITE) would split
  # on ';' and silently drop every C++ semicolon.
  set(header_buf "// Auto-generated from ${source_dir}.\n")
  string(APPEND header_buf "#pragma once\n#include <cstddef>\n")
  string(APPEND header_buf "namespace ${ns} {\n")
  string(APPEND header_buf "struct Asset { const char* name; const unsigned char* data; size_t size; };\n")

  set(source_buf "// Auto-generated from ${source_dir}.\n")
  string(APPEND source_buf "#include \"embedded_${output_namespace}.h\"\n")
  string(APPEND source_buf "namespace ${ns} {\n")

  set(index_lines "")
  foreach(path ${asset_files})
    if(IS_DIRECTORY "${path}")
      continue()
    endif()
    get_filename_component(name "${path}" NAME)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" symbol "${name}")
    if("${symbol}" MATCHES "^[0-9]")
      set(symbol "_${symbol}")
    endif()
    file(READ "${path}" hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE ",$" "" bytes "${bytes}")
    string(APPEND header_buf "extern const unsigned char ${symbol}_data[];\n")
    string(APPEND header_buf "extern const size_t ${symbol}_size;\n")
    string(APPEND source_buf "const unsigned char ${symbol}_data[] = { ${bytes} };\n")
    string(APPEND source_buf "const size_t ${symbol}_size = sizeof(${symbol}_data);\n")
    list(APPEND index_lines
        "  { \"${name}\", ${symbol}_data, sizeof(${symbol}_data) }")
  endforeach()

  string(JOIN ",\n" index_body ${index_lines})
  string(APPEND header_buf "extern const Asset kAll[];\n")
  string(APPEND header_buf "extern const size_t kAllCount;\n")
  string(APPEND header_buf "}  // namespace ${ns}\n")
  string(APPEND source_buf "const Asset kAll[] = {\n${index_body}\n};\n")
  string(APPEND source_buf "const size_t kAllCount = sizeof(kAll) / sizeof(kAll[0]);\n")
  string(APPEND source_buf "}  // namespace ${ns}\n")

  # configure_file COPYONLY skips the copy when bytes match, preserving mtime
  # so reconfigures don't force a multi-MB embedded_*.cc recompile.
  file(WRITE "${header_path}.in" "${header_buf}")
  configure_file("${header_path}.in" "${header_path}" COPYONLY)
  file(REMOVE "${header_path}.in")
  file(WRITE "${source_path}.in" "${source_buf}")
  configure_file("${source_path}.in" "${source_path}" COPYONLY)
  file(REMOVE "${source_path}.in")

  target_sources(${target} PRIVATE "${source_path}")
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

# xe_embed_compressed_bundle(target source_dir output_namespace)
#
# Packs every file in source_dir into a zlib-compressed concat blob and
# embeds it as a single byte array in embedded_bundle_<namespace>.{h,cc}.
# Symbols: xe::embedded_bundle_<ns>::kBundleData / kBundleSize. Decompress
# at runtime with EmbeddedBundle (xenia/patcher). Re-run cmake configure
# after adding/changing assets.
#
# The native C89 generator prepares the patch bundle during configuration.
function(xe_embed_compressed_bundle target source_dir output_namespace)
  if(NOT output_namespace STREQUAL "patches")
    message(FATAL_ERROR "Only the core's patch bundle is supported")
  endif()
  target_sources(${target} PRIVATE "${XENIA_GENERATED_DIR}/embedded_bundle_patches.cc")
  target_include_directories(${target} PRIVATE "${XENIA_GENERATED_DIR}")
endfunction()

# Embed raw checked-in modules. The native C89 generator reads bytes only.
function(xe_raw_shader_rules target shader_dir format subdir extension)
  file(GLOB _modules CONFIGURE_DEPENDS "${shader_dir}/bytecode/${subdir}/*.${extension}")
  file(RELATIVE_PATH _relative "${PROJECT_SOURCE_DIR}/src" "${shader_dir}")
  set(_outputs)
  foreach(_module IN LISTS _modules)
    get_filename_component(_name "${_module}" NAME_WE)
    set(_output "${XENIA_GENERATED_DIR}/generated/${_relative}/bytecode/${subdir}/${_name}.h")
    add_custom_command(OUTPUT "${_output}"
      COMMAND "${XENIA_HOST_CODEGEN}" shader "${_output}" "${_module}" "${_name}" "${format}"
      DEPENDS "${_module}" "${XENIA_HOST_CODEGEN}" VERBATIM)
    list(APPEND _outputs "${_output}")
  endforeach()
  add_custom_target(${target}-${format}-shaders DEPENDS ${_outputs})
  add_dependencies(${target} ${target}-${format}-shaders)
  target_include_directories(${target} BEFORE PRIVATE "${XENIA_GENERATED_DIR}/generated")
endfunction()

function(xe_shader_rules_spirv target shader_dir)
  xe_raw_shader_rules(${target} "${shader_dir}" spirv vulkan_spirv spv)
endfunction()

function(xe_shader_rules_dxil target shader_dir)
  xe_raw_shader_rules(${target} "${shader_dir}" dxil d3d12_dxil dxil)
endfunction()

# xe_force_c(files...) — compile the given sources as C.
function(xe_force_c)
  set_source_files_properties(${ARGN} PROPERTIES LANGUAGE C)
endfunction()

# xe_force_cxx(files...) — compile the given sources as C++.
function(xe_force_cxx)
  set_source_files_properties(${ARGN} PROPERTIES LANGUAGE CXX)
endfunction()

# xe_test_suite(name base_path LINKS lib1 lib2 ...)
#
# Creates a Catch2 test executable from *_test.cc files in base_path.
# Returns early (no target) if no test sources are found.
function(xe_test_suite name base_path)
  cmake_parse_arguments(ARG "" "" "LINKS" ${ARGN})

  file(GLOB _test_sources "${base_path}/*_test.cc")
  if(NOT _test_sources)
    return()
  endif()

  add_executable(${name}
    ${_test_sources}
    ${PROJECT_SOURCE_DIR}/tools/build/src/test_suite_main.cc
  )

  if(WIN32)
    target_sources(${name} PRIVATE
      ${PROJECT_SOURCE_DIR}/src/xenia/base/console_app_main_win.cc)
  else()
    target_sources(${name} PRIVATE
      ${PROJECT_SOURCE_DIR}/src/xenia/base/console_app_main_posix.cc)
  endif()

  target_compile_definitions(${name} PRIVATE
    "XE_TEST_SUITE_NAME=\"${name}\""
  )
  target_include_directories(${name} PRIVATE
    ${PROJECT_SOURCE_DIR}/tools/build
    ${PROJECT_SOURCE_DIR}/tools/build/src
    ${PROJECT_SOURCE_DIR}/tools/build/third_party/catch/include
  )
  if(ARG_LINKS)
    target_link_libraries(${name} PRIVATE ${ARG_LINKS})
  endif()
  xe_target_defaults(${name})
endfunction()

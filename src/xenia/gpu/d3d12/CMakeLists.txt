add_library(xenia-gpu-d3d12 STATIC)
xe_platform_sources(xenia-gpu-d3d12 ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(xenia-gpu-d3d12 PRIVATE "${PROJECT_SOURCE_DIR}/libretro/core_transfer_uint_d3d12.c")
xe_shader_rules_dxil(xenia-gpu-d3d12 ${CMAKE_CURRENT_SOURCE_DIR}/../shaders)
target_link_libraries(xenia-gpu-d3d12 PUBLIC
  fmt retro-common xenia-base xenia-gpu xenia-ui xenia-ui-d3d12
)
xe_target_defaults(xenia-gpu-d3d12)

# Shared guest analysis/modification settings use the owned SPIR-V classes;
# D3D12 emits guest DXIL directly and borrows raw host tessellation assets.
target_link_libraries(xenia-gpu-d3d12 PRIVATE xenia-spirv-writer)

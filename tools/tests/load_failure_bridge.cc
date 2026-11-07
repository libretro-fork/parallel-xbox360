/* Include the production API owner so tests can inspect its private ownership.
 * There is one owner object in this executable; no test hooks enter the DLL. */
#ifndef XENIA_LIBRETRO_VERSION
#define XENIA_LIBRETRO_VERSION "test"
#define XENIA_LIBRETRO_COMMIT "load-failure-test"
#endif
#include "libretro/libretro.cpp"
using xe::X_STATUS;
#ifdef _WIN32
DECLARE_int32(d3d12_adapter);
#endif
extern "C" int xe_load_probe_clean(void) {
    return !s_audio_registry && !xenia_emulator && !game_loaded &&
        !vulkan_hw_render_active && !d3d12_hw_render_active && !vk_hw &&
        !s_launch_on_context_reset && s_audio_channels == 2 &&
        !s_audio_float.batch && !s_audio_multi.batch_float && !s_audio_multi.batch_int16 &&
        gamedb::patches().empty() && s_audio_cadence.fraction == 0.0;
}
extern "C" int xe_load_probe_pending(void) {
    return s_audio_registry && s_launch_on_context_reset && !xenia_emulator && !game_loaded;
}
extern "C" int xe_load_probe_d3d12(void) {
#ifdef _WIN32
    return d3d12_available();
#else
    return 0;
#endif
}
extern "C" int xe_load_probe_providers(void) {
    xe::gpu::vulkan::VulkanGraphicsSystem vulkan;
    if (vulkan.Setup(nullptr,nullptr,false) != X_STATUS_UNSUCCESSFUL) return 0;
    vulkan.Shutdown();
#ifdef _WIN32
    cvars::d3d12_adapter = INT32_MAX;
    xe::gpu::d3d12::D3D12GraphicsSystem d3d12;
    if (d3d12.Setup(nullptr,nullptr,false) != X_STATUS_UNSUCCESSFUL) return 0;
    d3d12.Shutdown();
#endif
    return 1;
}

extern "C" int xe_load_probe_seed_patches(void) {
    if (gamedb::find_patches(0x4D5308AB, std::nullopt).empty()) return 0;
    publish_core_options();
    s_audio_cadence.fraction = 0.75;
    return 1;
}

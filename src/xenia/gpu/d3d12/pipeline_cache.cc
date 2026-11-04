/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/pipeline_cache.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/d3d12/d3d12_render_target_cache.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/dxil/guest_geometry.h"
#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/d3d12/d3d12_util.h"

#include "third_party/fmt/include/fmt/xchar.h"

DEFINE_int32(
    d3d12_pipeline_creation_threads, -1,
    "Number of threads used for graphics pipeline creation. -1 to calculate "
    "automatically (75% of logical CPU cores), a positive number to specify "
    "the number of threads explicitly (up to the number of logical CPU cores), "
    "0 to disable multithreaded pipeline creation.",
    "D3D12");
DEFINE_bool(d3d12_tessellation_wireframe, false,
            "Display tessellated surfaces as wireframe for debugging.",
            "D3D12");
DECLARE_bool(precise_interpolation);

namespace xe {
namespace gpu {
namespace d3d12 {

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/adaptive_quad_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/adaptive_triangle_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/continuous_quad_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/continuous_quad_4cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/continuous_triangle_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/continuous_triangle_3cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/discrete_quad_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/discrete_quad_4cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/discrete_triangle_1cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/discrete_triangle_3cp_hs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/placeholder_color_ps.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/placeholder_ps.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/tessellation_adaptive_vs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/tessellation_indexed_vs.h"
#include "xenia/gpu/shaders/bytecode/d3d12_dxil/ucode_interpreter_vs.h"
}  // namespace shaders

PipelineCache::PipelineCache(D3D12CommandProcessor& command_processor,
                             const RegisterFile& register_file,
                             const D3D12RenderTargetCache& render_target_cache,
                             bool bindless_resources_used,
                             bool zpd_hybrid_supported)
    : command_processor_(command_processor),
      register_file_(register_file),
      render_target_cache_(render_target_cache),
      bindless_resources_used_(bindless_resources_used),
      zpd_hybrid_supported_(zpd_hybrid_supported),
      guest_shader_cache_(*this, register_file, render_target_cache) {}

PipelineCache::~PipelineCache() { Shutdown(); }

bool PipelineCache::Initialize() {
  // Shared guest feature/modification settings; native stages read raw ucode.
  if (!guest_shader_cache_.Initialize()) {
    return false;
  }

  if (render_target_cache_.GetPath() !=
      RenderTargetCache::Path::kPixelShaderInterlock) {
    XeDxilDepthOnlyOptions options = {};
    options.guest_linkage = 1;
    options.system_constants_bytes = sizeof(SpirvShaderTranslator::SystemConstants);
    options.counter_index_word = offsetof(SpirvShaderTranslator::SystemConstants,
                                          zpd_fsi_counter_index) / sizeof(uint32_t);
    options.counter_stride_bytes = XenosZPDReport::kCounterSizeBytes;
    if (!xe_dxil_build_depth_only_set(
            &options, render_target_cache_.depth_float24_convert_in_pixel_shader(),
            zpd_hybrid_supported_, native_depth_only_pixel_shaders_)) {
      XELOGE("Failed to generate native host-depth pixel shaders");
      return false;
    }
  }

  if (render_target_cache_.GetPath() ==
      RenderTargetCache::Path::kPixelShaderInterlock) {
    XeDxilRovDepthOptions options = {};
    options.scale_x = render_target_cache_.draw_resolution_scale_x();
    options.scale_y = render_target_cache_.draw_resolution_scale_y();
    options.full_counters = cvars::occlusion_query_full_counters;
    options.system_constants_bytes = sizeof(SpirvShaderTranslator::SystemConstants);
    options.flags_word = offsetof(SpirvShaderTranslator::SystemConstants, flags) / 4;
    options.pitch_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_32bpp_tile_pitch_dwords_scaled) / 4;
    options.depth_base_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_depth_base_dwords_scaled) / 4;
    options.counter_word = offsetof(SpirvShaderTranslator::SystemConstants, zpd_fsi_counter_index) / 4;
    options.front_scale_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_front_scale) / 4;
    options.back_scale_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_back_scale) / 4;
    options.front_offset_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_front_offset) / 4;
    options.back_offset_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_back_offset) / 4;
    options.front_masks_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_front_reference_masks) / 4;
    options.back_masks_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_back_reference_masks) / 4;
    options.front_ops_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_front_func_ops) / 4;
    options.back_ops_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_back_func_ops) / 4;
    options.depth_stencil_mask = SpirvShaderTranslator::kSysFlag_FSIDepthStencil;
    options.float24_mask = SpirvShaderTranslator::kSysFlag_DepthFloat24;
    options.depth_write_mask = SpirvShaderTranslator::kSysFlag_FSIDepthWrite;
    options.stencil_mask = SpirvShaderTranslator::kSysFlag_FSIStencilTest;
    options.depth_function_shift = SpirvShaderTranslator::kSysFlag_FSIDepthPassIfLess_Shift;
    if (!xe_dxil_build_rov_depth_set(&options, native_rov_depth_pixel_shaders_)) {
      XELOGE("Failed to generate native ROV depth-only pixel shaders");
      return false;
    }
    options.guest_linkage = 1;
    if (!xe_dxil_build_rov_depth_set(&options, native_guest_rov_depth_pixel_shaders_)) {
      return false;
    }
  }

  uint32_t logical_processor_count = xe::threading::logical_processor_count();
  if (!logical_processor_count) {
    // Pick some reasonable amount if couldn't determine the number of cores.
    logical_processor_count = 6;
  }
  // Set up even without creation threads - the storage warm-up starts some of
  // its own.
  creation_queue_.Initialize(
      "D3D12 Pipelines",
      [this](Pipeline* pipeline, uint8_t*) {
        return CreateQueuedPipeline(pipeline);
      },
      [this](Pipeline* pipeline, ID3D12PipelineState* state) {
        StoreCreatedPipeline(pipeline, state);
      },
      {});
  if (cvars::d3d12_pipeline_creation_threads != 0) {
    size_t creation_thread_count;
    if (cvars::d3d12_pipeline_creation_threads < 0) {
      creation_thread_count =
          std::max(logical_processor_count * 3 / 4, uint32_t(1));
    } else {
      creation_thread_count =
          std::min(uint32_t(cvars::d3d12_pipeline_creation_threads),
                   logical_processor_count);
    }
    creation_queue_.SetThreadCount(creation_thread_count);
  }
  return true;
}

void PipelineCache::Shutdown() {
  // Shut down all threads, before destroying the pipelines since they may be
  // creating them. Nothing can be parked for publishing once they are joined,
  // so release whatever was still held back.
  creation_queue_.Shutdown([](Pipeline*, ID3D12PipelineState* state) {
    if (state) state->Release();
  });

  xe_dxil_depth_only_set_free(native_depth_only_pixel_shaders_);
  xe_dxil_rov_depth_set_free(native_rov_depth_pixel_shaders_);
  xe_dxil_rov_depth_set_free(native_guest_rov_depth_pixel_shaders_);

  // Shut down the persistent shader / pipeline storage.
  ShutdownShaderStorage();

  // Release placeholder pipelines that were swapped out but not yet reaped (the
  // creation threads are joined, so no more can be queued). The GPU is idle by
  // shutdown, so there is nothing to wait for.
  for (auto& deferred : deferred_destroy_pipelines_) {
    deferred.first->Release();
  }
  deferred_destroy_pipelines_.clear();

  // Destroy all pipelines.
  current_pipeline_ = nullptr;
  for (auto it : pipelines_) {
    ID3D12PipelineState* state =
        it.second->state.load(std::memory_order_acquire);
    if (state) {
      state->Release();
    }
    delete it.second;
  }
  pipelines_.clear();
  COUNT_profile_set("gpu/pipeline_cache/pipelines", 0);

  // Destroy all shaders.
  for (auto it : shaders_) {
    delete it.second;
  }
  shaders_.clear();
}

void PipelineCache::InitializeShaderStorage(
    const std::filesystem::path& cache_root, uint32_t title_id, bool blocking,
    std::function<void()> completion_callback) {
  ShutdownShaderStorage();

  bool edram_rov_used = render_target_cache_.GetPath() ==
                        RenderTargetCache::Path::kPixelShaderInterlock;

  ShaderStorageWriter<PipelineStoredDescription>::PipelineStorageConfig
      pipeline_config;
  // Full ZPD counters change every ROV pixel shader, so they get their own
  // storage.
  pipeline_config.file_suffix = fmt::format(
      ".{}{}.d3d12.xpso", edram_rov_used ? "rov" : "rtv",
      edram_rov_used && cvars::occlusion_query_full_counters ? "-fc" : "");
  pipeline_config.api_magic = edram_rov_used ? 0x4F525844 : 0x54525844;
  // Sum so a bump of either version invalidates - both only ever move up.
  pipeline_config.version = PipelineDescription::kVersion +
                            SpirvShaderTranslator::Modification::kVersion;

  uint32_t storage_index = storage_writer_.storage_index() + 1;

  PipelineStorageRecords pipeline_stored_descriptions(nullptr, xe_file_records_close);
  if (!storage_writer_.InitializeShaderStorage(
          cache_root, title_id, pipeline_config,
          // Shader load callback.
          [&](xenos::ShaderType type, const uint32_t* ucode_dwords,
              uint32_t ucode_dword_count, uint64_t ucode_data_hash) {
            SpirvShader* shader = LoadShader(
                type, ucode_dwords, ucode_dword_count, ucode_data_hash);
            if (shader->ucode_storage_index() == storage_index) {
              return true;  // Already loaded.
            }
            shader->set_ucode_storage_index(storage_index);
            return true;
          },
          // Shader analyze callback. Only the ucode analysis is needed.
          [this](const std::set<std::pair<uint64_t, uint64_t>>
                     & translations_needed) {
            AnalyzeShadersForStorage(translations_needed);
          },
          pipeline_stored_descriptions)) {
    return;
  }
  shader_storage_file_flush_needed_ = false;
  pipeline_storage_file_flush_needed_ = false;

  // Create the pipelines.
  const size_t pipeline_record_count = xe_file_records_count(pipeline_stored_descriptions.get());
  if (pipeline_record_count != 0) {
    uint64_t pipeline_creation_start_ = xe::Clock::QueryHostTickCount();

    // Launch additional creation threads to use all cores to create
    // pipelines faster. Will also be using the main thread, so minus 1.
    size_t logical_processor_count = xe::threading::logical_processor_count();
    if (!logical_processor_count) {
      logical_processor_count = 6;
    }
    size_t creation_thread_original_count = creation_queue_.thread_count();
    creation_queue_.SetThreadCount(std::max(
        std::min(pipeline_record_count, logical_processor_count) -
            size_t(1),
        creation_thread_original_count));

    size_t pipelines_created = 0;
    size_t pipelines_already_exist = 0;
    size_t pipelines_vs_not_found = 0;
    size_t pipelines_vs_translation_missing = 0;
    size_t pipelines_ps_not_found = 0;
    size_t pipelines_ps_translation_missing = 0;
    size_t pipelines_root_sig_failed = 0;
    PipelineStoredDescription pipeline_stored_description;
    size_t record_index;
    for (record_index = 0; record_index < pipeline_record_count; ++record_index) {
      if (!xe_file_records_read(pipeline_stored_descriptions.get(), record_index,
                               &pipeline_stored_description, sizeof(pipeline_stored_description))) break;
      const PipelineDescription& pipeline_description =
          pipeline_stored_description.description;
      // TODO(Triang3l): On Vulkan, skip pipelines requiring unsupported device
      // features (to keep the cache files mostly shareable across devices).
      // Skip already known pipelines - those have already been enqueued.
      auto found_range =
          pipelines_.equal_range(pipeline_stored_description.description_hash);
      bool pipeline_found = false;
      for (auto it = found_range.first; it != found_range.second; ++it) {
        Pipeline* found_pipeline = it->second;
        if (!std::memcmp(&found_pipeline->description.description,
                         &pipeline_description, sizeof(pipeline_description))) {
          pipeline_found = true;
          break;
        }
      }
      if (pipeline_found) {
        ++pipelines_already_exist;
        continue;
      }

      // Re-translate the guest shaders to DXIL at the current resolution from
      // the stored guest modifications, then create the PSO.
      auto vertex_shader_it =
          shaders_.find(pipeline_description.vertex_shader_hash);
      if (vertex_shader_it == shaders_.end()) {
        ++pipelines_vs_not_found;
        continue;
      }
      SpirvShader* native_vertex_shader = vertex_shader_it->second;
      SpirvShader* native_pixel_shader = nullptr;
      if (pipeline_description.pixel_shader_hash) {
        auto pixel_shader_it =
            shaders_.find(pipeline_description.pixel_shader_hash);
        if (pixel_shader_it == shaders_.end()) {
          ++pipelines_ps_not_found;
          continue;
        }
        native_pixel_shader = pixel_shader_it->second;
      }
      // Value-initialize so the runtime pointer fields (native_*_dxil,
      // native_*_translation) start null. CreateD3D12Pipeline reads them and
      // uninitialized garbage crashes intermittently. The description bits
      // are overwritten by the memcpy below.
      PipelineRuntimeDescription native_runtime_description = {};
      std::memcpy(&native_runtime_description.description, &pipeline_description,
                  sizeof(pipeline_description));
      // Main-thread work is only the cheap part: the VS/PS translation
      // objects for shader metadata (CreateD3D12Pipeline reads ucode hash +
      // modification even on the native path). The expensive
      // guest-to-DXIL emission is deferred to a creation thread
      // (BuildNativePipelineDxil in EnsurePipelineShadersTranslated), so
      // startup is not serialized here.
      native_runtime_description.vertex_shader =
          native_vertex_shader->GetOrCreateTranslation(
              pipeline_description.vertex_shader_modification);
      native_runtime_description.native_vertex_translation =
          EnsureGuestTranslation(
              *native_vertex_shader,
              pipeline_description.vertex_shader_modification);
      if (native_pixel_shader) {
        native_runtime_description.pixel_shader =
            native_pixel_shader->GetOrCreateTranslation(
                pipeline_description.pixel_shader_modification);
        native_runtime_description.native_pixel_translation =
            EnsureGuestTranslation(
                *native_pixel_shader,
                pipeline_description.pixel_shader_modification);
      }
      native_runtime_description.root_signature =
          command_processor_.GetNativeRootSignature();
      if (!native_runtime_description.vertex_shader ||
          !native_runtime_description.native_vertex_translation) {
        ++pipelines_vs_translation_missing;
        continue;
      }
      Pipeline* new_pipeline = new Pipeline;
      new_pipeline->from_storage = true;
      std::memcpy(&new_pipeline->description, &native_runtime_description,
                  sizeof(native_runtime_description));
      pipelines_.emplace(pipeline_stored_description.description_hash,
                         new_pipeline);
      COUNT_profile_set("gpu/pipeline_cache/pipelines", pipelines_.size());
      if (creation_queue_.has_threads()) {
        // Creation thread builds the Native DXIL off the main thread (the
        // deferred block in EnsurePipelineShadersTranslated), then the PSO.
        // Nothing is drawing these yet, so they publish as they are built.
        new_pipeline->creation_pending.store(true, std::memory_order_relaxed);
        creation_queue_.PushUnordered(new_pipeline);
      } else {
        // No creation threads: build the DXIL + create the PSO here.
        if (BuildNativePipelineDxil(
                new_pipeline->description.native_vertex_translation,
                new_pipeline->description.native_pixel_translation,
                new_pipeline->description)) {
          new_pipeline->state.store(
              CreateD3D12Pipeline(new_pipeline->description),
              std::memory_order_release);
        }
      }
      ++pipelines_created;
    }

    if (creation_queue_.has_threads()) {
      if (blocking) {
        // Blocking mode: help drain the queue on this thread, then let
        // the extra threads go and wait for the ones that
        // are still finishing a pipeline they popped. This ensures all cached
        // pipelines are fully created before the game starts, populating the
        // driver's shader cache.
        creation_queue_.DrainOnCallingThread(nullptr);
        creation_queue_.SetThreadCount(creation_thread_original_count);
        creation_queue_.AwaitCompletion();
      } else {
        // Non-blocking mode: the background threads have it, and invoke the
        // callback when they are done - or leave it to the tail of this
        // function if they are already done.
        creation_queue_.TakeCompletionCallback(completion_callback);
      }
    }

    XELOGI(
        "Pipeline cache loaded: {} created, {} already exist, {} total stored",
        pipelines_created, pipelines_already_exist,
        pipeline_record_count);
    if (pipelines_vs_not_found || pipelines_vs_translation_missing ||
        pipelines_ps_not_found || pipelines_ps_translation_missing ||
        pipelines_root_sig_failed) {
      XELOGI(
          "Pipeline cache skipped: {} VS not found, {} VS translation missing, "
          "{} PS not found, {} PS translation missing, {} root sig failed",
          pipelines_vs_not_found, pipelines_vs_translation_missing,
          pipelines_ps_not_found, pipelines_ps_translation_missing,
          pipelines_root_sig_failed);
    }
    XELOGI("Pipeline creation took {} milliseconds",
           (xe::Clock::QueryHostTickCount() - pipeline_creation_start_) * 1000 /
               xe::Clock::QueryHostTickFrequency());
  }

  shader_storage_title_id_ = title_id;

  // Invoke completion callback if provided (for blocking mode or when no
  // background work was needed). For non-blocking mode with background work,
  // the creation queue took it and invokes it when done.
  if (completion_callback) {
    completion_callback();
  }
}

void PipelineCache::ShutdownShaderStorage() {
  // Shut down the storage writer (closes files, stops write thread).
  storage_writer_.ShutdownShaderStorage();
  shader_storage_file_flush_needed_ = false;
  pipeline_storage_file_flush_needed_ = false;
  shader_storage_title_id_ = 0;
}

void PipelineCache::EndSubmission() {
  if (shader_storage_file_flush_needed_ ||
      pipeline_storage_file_flush_needed_) {
    storage_writer_.RequestFlush(shader_storage_file_flush_needed_,
                                 pipeline_storage_file_flush_needed_);
    shader_storage_file_flush_needed_ = false;
    pipeline_storage_file_flush_needed_ = false;
  }
  // Release placeholder pipelines the GPU is now done with.
  ProcessDeferredDestructions();
  // Don't wait for pipeline creation - let background threads work
  // asynchronously. Bindless draws use a placeholder pipeline meanwhile.
  // Bindful draws are skipped until the pipeline is ready. This avoids
  // frame-time spikes from blocking on pipeline creation.
  creation_queue_.Notify();
}

void PipelineCache::ProcessDeferredDestructions() {
  std::vector<ID3D12PipelineState*> pipelines_to_destroy;
  uint64_t completed_submission = command_processor_.GetCompletedSubmission();
  {
    std::lock_guard<std::mutex> lock(deferred_destroy_mutex_);
    auto it = deferred_destroy_pipelines_.begin();
    while (it != deferred_destroy_pipelines_.end()) {
      if (it->second <= completed_submission) {
        pipelines_to_destroy.push_back(it->first);
        it = deferred_destroy_pipelines_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (ID3D12PipelineState* pipeline : pipelines_to_destroy) {
    pipeline->Release();
  }
}

bool PipelineCache::IsCreatingPipelines() { return creation_queue_.IsBusy(); }

void PipelineCache::AwaitPipelineCompletion() {
  creation_queue_.AwaitCompletion();
}

ID3D12PipelineState* PipelineCache::AwaitD3D12PipelineByHandle(void* handle) {
  ID3D12PipelineState* pipeline = GetD3D12PipelineByHandle(handle);
  if (pipeline != nullptr) {
    return pipeline;
  }
  AwaitPipelineCompletion();
  return GetD3D12PipelineByHandle(handle);
}

ID3D12PipelineState* PipelineCache::AwaitRealD3D12PipelineByHandle(
    void* handle) {
  // A non-null, non-placeholder state is already the real pipeline.
  if (GetD3D12PipelineByHandle(handle) != nullptr &&
      !IsPlaceholderPipeline(handle)) {
    return GetD3D12PipelineByHandle(handle);
  }
  // Drain all background creation. Afterwards the real pipeline has been
  // swapped in (or its creation failed, leaving the placeholder or nullptr).
  AwaitPipelineCompletion();
  return GetD3D12PipelineByHandle(handle);
}

SpirvShader* PipelineCache::LoadShader(xenos::ShaderType shader_type,
                                       const uint32_t* host_address,
                                       uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  return LoadShader(shader_type, host_address, dword_count,
                    XXH3_64bits(host_address, dword_count * sizeof(uint32_t)));
}

SpirvShader* PipelineCache::LoadShader(xenos::ShaderType shader_type,
                                       const uint32_t* host_address,
                                       uint32_t dword_count,
                                       uint64_t data_hash) {
  auto it = shaders_.find(data_hash);
  if (it != shaders_.end()) {
    // Shader has been previously loaded.
    return it->second;
  }
  // Always create the shader and stash it away.
  // We need to track it even if it fails translation so we know not to try
  // again.
  SpirvShader* shader =
      new SpirvShader(shader_type, data_hash, host_address, dword_count);
  shaders_.emplace(data_hash, shader);
  return shader;
}

uint64_t PipelineCache::GetCurrentSpirvVertexShaderModification(
    const Shader& shader, Shader::HostVertexShaderType host_vertex_shader_type,
    uint32_t interpolator_mask) const {
  // D3D12 never produces kPointListAsTriangleStrip (points expand via the
  // built-in geometry shader), so ps_param_gen_used is irrelevant here.
  return guest_shader_cache_.GetVertexShaderModification(
      shader, host_vertex_shader_type, interpolator_mask,
      /*ps_param_gen_used=*/false);
}

uint64_t PipelineCache::GetCurrentSpirvPixelShaderModification(
    const Shader& shader, uint32_t interpolator_mask, uint32_t param_gen_pos,
    reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask, bool apply_polygon_offset_in_shader) const {
  return guest_shader_cache_.GetPixelShaderModification(
      shader, interpolator_mask, param_gen_pos, normalized_depth_control,
      normalized_color_mask, apply_polygon_offset_in_shader);
}

bool PipelineCache::precise_interpolation_supported() const {
  // Manual barycentric interpolation, only when the device supports
  // SV_Barycentrics.
  return cvars::precise_interpolation &&
         command_processor_.GetD3D12Provider().AreBarycentricsSupported();
}

std::unique_ptr<SpirvShaderTranslator> PipelineCache::CreateTranslator() const {
  SpirvShaderTranslator::Features spirv_features(/*all=*/true);
  // Pixel (not sample) interlock: maps to D3D12's per-pixel rasterizer-ordered
  // views used by the native render-output stage.
  spirv_features.fragment_shader_sample_interlock = false;
  // Manual barycentric interpolation (gl_BaryCoordKHR + PerVertexKHR).
  // Native DXIL uses SV_Barycentrics +
  // attributeAtVertex.
  spirv_features.fragment_shader_barycentric = true;
  spirv_features.signed_zero_inf_nan_preserve_float32 = false;
  spirv_features.denorm_flush_to_zero_float32 = true;
  spirv_features.rounding_mode_rte_float32 = false;
  // EDRAM fragment shader interlock (the ROV path) when the render target cache
  // is in its rasterizer-ordered output path; host render targets otherwise.
  bool edram_fragment_shader_interlock =
      render_target_cache_.GetPath() ==
      RenderTargetCache::Path::kPixelShaderInterlock;
  return std::make_unique<SpirvShaderTranslator>(
      spirv_features, render_target_cache_.msaa_2x_supported(),
      /*native_2x_msaa_no_attachments=*/false, edram_fragment_shader_interlock,
      precise_interpolation_supported(),
      render_target_cache_.draw_resolution_scale_x(),
      render_target_cache_.draw_resolution_scale_y());
}

Shader::Translation* PipelineCache::EnsureGuestTranslation(
    SpirvShader& shader, uint64_t spirv_modification) {
  if (!shader.is_ucode_analyzed()) {
    StringBuffer ucode_disasm;
    shader.AnalyzeUcode(ucode_disasm);
  }
  return shader.GetOrCreateTranslation(spirv_modification);
}

// Native compilation does not mark the SPIR-V translation as complete. Consult
// its own cache before choosing an interpreter or dropping a cached VS draw.
bool PipelineCache::IsGuestVertexReady(const Shader::Translation& translation) {
  std::lock_guard<std::mutex> lock(native_dxil_cache_mutex_);
  auto shader = native_vertex_dxil_cache_.find(translation.shader().ucode_data_hash());
  if (shader == native_vertex_dxil_cache_.end()) return false;
  auto binary = shader->second.find(translation.modification());
  return binary != shader->second.end() && binary->second != nullptr;
}
namespace {
bool BuildNativeTextureLayout(const SpirvShader& shader, XeDxilTextureLayout& layout, unsigned pixel) {
  layout = {};
  if (shader.bindings_ready()) {
    const auto& textures = shader.GetTextureBindingsAfterTranslation();
    const auto& samplers = shader.GetSamplerBindingsAfterTranslation();
    if (textures.size() > XE_DXIL_TEXTURE_BINDINGS_MAX || samplers.size() > XE_DXIL_SAMPLER_BINDINGS_MAX) return false;
    for (const auto& binding : textures) {
      auto& out = layout.textures[layout.texture_count++];
      out.fetch_constant = binding.fetch_constant; out.dimension = uint32_t(binding.dimension); out.is_signed = binding.is_signed;
    }
    for (const auto& binding : samplers) {
      auto& out = layout.samplers[layout.sampler_count++];
      out.fetch_constant = binding.fetch_constant; out.mag_filter = uint32_t(binding.mag_filter); out.min_filter = uint32_t(binding.min_filter);
      out.mip_filter = uint32_t(binding.mip_filter); out.aniso_filter = uint32_t(binding.aniso_filter);
    }
    layout.used_texture_mask = shader.GetUsedTextureMaskAfterTranslation();
  }
  for (const auto& binding : shader.texture_bindings()) {
    const auto& source = binding.fetch_instr;
    if (!source.result.GetUsedResultComponents()) continue;
    XeDxilTextureInstruction instruction = {};
    instruction.opcode = uint32_t(source.opcode); instruction.constant_index = binding.fetch_constant; instruction.dimension = uint32_t(source.dimension);
    instruction.mag_filter = uint32_t(source.attributes.mag_filter); instruction.min_filter = uint32_t(source.attributes.min_filter);
    instruction.mip_filter = uint32_t(source.attributes.mip_filter); instruction.aniso_filter = uint32_t(source.attributes.aniso_filter);
    instruction.computed_lod = source.attributes.use_computed_lod; instruction.register_gradients = source.attributes.use_register_gradients;
    if (!xe_dxil_texture_layout_add(&layout,&instruction,pixel)) return false;
  }
  return true;
}
bool PublishNativeTextureLayout(SpirvShader& shader, const XeDxilTextureLayout& layout) {
  std::vector<SpirvShader::TextureBinding> textures;
  std::vector<SpirvShader::SamplerBinding> samplers;
  textures.reserve(layout.texture_count); samplers.reserve(layout.sampler_count);
  for (unsigned i = 0; i < layout.texture_count; ++i) {
    const auto& in = layout.textures[i]; auto& out = textures.emplace_back();
    std::memset(&out,0,sizeof(out)); out.fetch_constant = in.fetch_constant;
    out.dimension = xenos::FetchOpDimension(in.dimension); out.is_signed = in.is_signed;
  }
  for (unsigned i = 0; i < layout.sampler_count; ++i) {
    const auto& in = layout.samplers[i]; auto& out = samplers.emplace_back();
    std::memset(&out,0,sizeof(out)); out.fetch_constant = in.fetch_constant;
    out.mag_filter = xenos::TextureFilter(in.mag_filter); out.min_filter = xenos::TextureFilter(in.min_filter);
    out.mip_filter = xenos::TextureFilter(in.mip_filter); out.aniso_filter = xenos::AnisoFilter(in.aniso_filter);
  }
  return shader.PublishNativeBindings(std::move(textures),std::move(samplers),layout.used_texture_mask);
}
XeDxilTextureStageOptions NativeTextureOptions(const XeDxilTextureLayout& layout, unsigned pixel, uint32_t scale_x, uint32_t scale_y) {
  XeDxilTextureStageOptions options = {};
  options.layout = &layout; options.pixel_shader = pixel;
  options.system_constants_bytes = sizeof(SpirvShaderTranslator::SystemConstants);
  options.signs_word = offsetof(SpirvShaderTranslator::SystemConstants,texture_swizzled_signs)/4;
  options.integer_scale_word = offsetof(SpirvShaderTranslator::SystemConstants,texture_integer_scale_bits)/4;
  options.texture_scale_x = scale_x; options.texture_scale_y = scale_y;
  options.scale_resolved_offsets = cvars::draw_resolution_scaled_texture_offsets;
  options.resolved_word = offsetof(SpirvShaderTranslator::SystemConstants,textures_resolved)/4;
  return options;
}
bool NativeTessellationHosts(unsigned mode, Shader::HostVertexShaderType host,
    D3D12_SHADER_BYTECODE& vertex, D3D12_SHADER_BYTECODE& hull) {
  static_assert(unsigned(Shader::HostVertexShaderType::kTriangleDomainCPIndexed) == 3);
  static_assert(unsigned(Shader::HostVertexShaderType::kTriangleDomainPatchIndexed) == 4);
  static_assert(unsigned(Shader::HostVertexShaderType::kQuadDomainCPIndexed) == 5);
  static_assert(unsigned(Shader::HostVertexShaderType::kQuadDomainPatchIndexed) == 6);
  static const D3D12_SHADER_BYTECODE assets[] = {
    {shaders::tessellation_indexed_vs,sizeof(shaders::tessellation_indexed_vs)},
    {shaders::tessellation_adaptive_vs,sizeof(shaders::tessellation_adaptive_vs)},
    {shaders::discrete_triangle_1cp_hs,sizeof(shaders::discrete_triangle_1cp_hs)},
    {shaders::discrete_triangle_3cp_hs,sizeof(shaders::discrete_triangle_3cp_hs)},
    {shaders::discrete_quad_1cp_hs,sizeof(shaders::discrete_quad_1cp_hs)},
    {shaders::discrete_quad_4cp_hs,sizeof(shaders::discrete_quad_4cp_hs)},
    {shaders::continuous_triangle_1cp_hs,sizeof(shaders::continuous_triangle_1cp_hs)},
    {shaders::continuous_triangle_3cp_hs,sizeof(shaders::continuous_triangle_3cp_hs)},
    {shaders::continuous_quad_1cp_hs,sizeof(shaders::continuous_quad_1cp_hs)},
    {shaders::continuous_quad_4cp_hs,sizeof(shaders::continuous_quad_4cp_hs)},
    {shaders::adaptive_triangle_hs,sizeof(shaders::adaptive_triangle_hs)},
    {shaders::adaptive_quad_hs,sizeof(shaders::adaptive_quad_hs)}
  };
  unsigned vs, hs;
  if (!xe_dxil_guest_tessellation_hosts(mode,unsigned(host),&vs,&hs)) return false;
  vertex = assets[vs]; hull = assets[hs]; return true;
}
}  // namespace

const XeDxilShaderBinary* PipelineCache::GetGuestNativeVertexDxil(
    const Shader& shader, uint64_t modification) {
  SpirvShaderTranslator::Modification mod(modification);
  unsigned domain = 0, control_points = 0;
  if (!shader.is_ucode_analyzed() ||
      (mod.vertex.host_vertex_shader_type != Shader::HostVertexShaderType::kVertex &&
       !xe_dxil_guest_domain_config(unsigned(mod.vertex.host_vertex_shader_type),&domain,&control_points)) ||
      shader.kills_pixels()) {
    return nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(native_dxil_cache_mutex_);
    auto& cache = native_vertex_dxil_cache_[shader.ucode_data_hash()];
    auto found = cache.find(modification);
    if (found != cache.end()) return found->second.get();
  }
  auto* binding_shader = const_cast<SpirvShader*>(dynamic_cast<const SpirvShader*>(&shader));
  if (!binding_shader) return nullptr;
  std::unique_lock<std::mutex> bindings_lock(binding_shader->resource_bindings_mutex());
  XeDxilTextureLayout texture_layout = {};
  if (!BuildNativeTextureLayout(*binding_shader,texture_layout,0)) return nullptr;
  auto texture_options = NativeTextureOptions(texture_layout,0,render_target_cache_.draw_resolution_scale_x(),render_target_cache_.draw_resolution_scale_y());
  XeDxilGuestVertexOptions options = {};
  options.program.memexport_mask = shader.memexport_eM_written();
  options.domain = domain; options.control_points = control_points;
  if (texture_layout.texture_count) options.textures = &texture_options;
  const auto& constants = shader.constant_register_map();
  options.program.register_count = std::max(
      shader.register_static_address_bound(),
      uint32_t(mod.vertex.dynamic_addressable_register_count));
  if (domain) options.program.register_count = std::max(options.program.register_count,2u);
  options.program.float_constant_count = constants.float_count;
  options.program.float_dynamic_addressing = constants.float_dynamic_addressing;
  options.program.float_bitmap = constants.float_bitmap;
  options.system_constants_bytes = sizeof(SpirvShaderTranslator::SystemConstants);
  options.flags_word = offsetof(SpirvShaderTranslator::SystemConstants, flags) / 4;
  options.index_endian_word = offsetof(SpirvShaderTranslator::SystemConstants, vertex_index_endian) / 4;
  options.index_base_word = offsetof(SpirvShaderTranslator::SystemConstants, vertex_base_index) / 4;
  options.ndc_scale_word = offsetof(SpirvShaderTranslator::SystemConstants, ndc_scale) / 4;
  options.ndc_offset_word = offsetof(SpirvShaderTranslator::SystemConstants, ndc_offset) / 4;
  options.clip_planes_word = offsetof(SpirvShaderTranslator::SystemConstants, user_clip_planes) / 4;
  options.interpolator_mask = mod.vertex.interpolator_mask;
  options.clip_planes = mod.vertex.user_clip_plane_cull ? 0 : mod.vertex.user_clip_plane_count;
  options.cull_planes = mod.vertex.user_clip_plane_cull ? mod.vertex.user_clip_plane_count : 0;
  options.point_size = mod.vertex.output_point_parameters;
  options.vertex_kill = (shader.writes_point_size_edge_flag_kill_vertex() & 4) != 0;
  options.vertex_kill_and = mod.vertex.vertex_kill_and;
  options.w_not_reciprocal_mask = SpirvShaderTranslator::kSysFlag_WNotReciprocal;
  options.xy_divided_mask = SpirvShaderTranslator::kSysFlag_XYDividedByW;
  options.z_divided_mask = SpirvShaderTranslator::kSysFlag_ZDividedByW;
  OwnedNativeDxil binary(xe_dxil_build_guest_vertex_owned(
      &options, shader.ucode_dwords(), shader.ucode_dword_count(),
      shader.cf_pair_index_bound() * 2), &xe_dxil_shader_binary_delete);
  if (binary && !PublishNativeTextureLayout(*binding_shader,texture_layout)) return nullptr;
  bindings_lock.unlock();
  std::lock_guard<std::mutex> lock(native_dxil_cache_mutex_);
  auto& cache = native_vertex_dxil_cache_[shader.ucode_data_hash()];
  return cache.try_emplace(modification, std::move(binary)).first->second.get();
}
const XeDxilShaderBinary* PipelineCache::GetGuestNativePixelDxil(
    const Shader& shader, uint64_t modification) {
  SpirvShaderTranslator::Modification mod(modification);
  if (!shader.is_ucode_analyzed()) return nullptr;
  bool edram_rov_used = render_target_cache_.GetPath() == RenderTargetCache::Path::kPixelShaderInterlock;
  {
    std::lock_guard<std::mutex> lock(native_dxil_cache_mutex_);
    auto& cache = native_pixel_dxil_cache_[shader.ucode_data_hash()];
    auto found = cache.find(modification);
    if (found != cache.end()) return found->second.get();
  }
  static_assert(XE_DXIL_BLEND_SOURCE_COLOR == uint32_t(xenos::BlendFactor::kSrcColor));
  static_assert(XE_DXIL_BLEND_INVERSE_SOURCE_COLOR == uint32_t(xenos::BlendFactor::kOneMinusSrcColor));
  auto* binding_shader = const_cast<SpirvShader*>(dynamic_cast<const SpirvShader*>(&shader));
  if (!binding_shader) return nullptr;
  std::unique_lock<std::mutex> bindings_lock(binding_shader->resource_bindings_mutex());
  XeDxilTextureLayout texture_layout = {};
  if (!BuildNativeTextureLayout(*binding_shader,texture_layout,1)) return nullptr;
  auto texture_options = NativeTextureOptions(texture_layout,1,render_target_cache_.draw_resolution_scale_x(),render_target_cache_.draw_resolution_scale_y());
  XeDxilGuestRopOptions rop_options = {};
  XeDxilGuestPixelOptions options = {};
  options.program.memexport_mask = shader.memexport_eM_written();
  if (texture_layout.texture_count) options.textures = &texture_options;
  const auto& constants = shader.constant_register_map();
  options.program.register_count = std::max(shader.register_static_address_bound(),
      uint32_t(mod.pixel.dynamic_addressable_register_count));
  options.program.float_constant_count = constants.float_count;
  options.program.float_dynamic_addressing = constants.float_dynamic_addressing;
  options.program.float_bitmap = constants.float_bitmap;
  options.interpolator_mask = mod.pixel.interpolator_mask;
  options.point_coordinate_mask = shader.point_fetch_coordinate_registers();
  options.centroid_mask = mod.pixel.interpolators_centroid;
  options.color_mask = shader.writes_color_targets();
  options.writes_depth = shader.writes_depth();
  options.system_constants_bytes = sizeof(SpirvShaderTranslator::SystemConstants);
  options.flags_word = offsetof(SpirvShaderTranslator::SystemConstants, flags) / 4;
  options.alpha_reference_word = offsetof(SpirvShaderTranslator::SystemConstants, alpha_test_reference) / 4;
  options.alpha_to_mask_word = offsetof(SpirvShaderTranslator::SystemConstants, alpha_to_mask) / 4;
  options.color_bias_word = offsetof(SpirvShaderTranslator::SystemConstants, color_exp_bias) / 4;
  options.blend_constant_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_blend_constant) / 4;
  options.alpha_function_shift = SpirvShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  options.gamma_mask = SpirvShaderTranslator::kSysFlag_ConvertColor0ToGamma;
  options.msaa_shift = SpirvShaderTranslator::kSysFlag_MsaaSamples_Shift;
  options.depth_mode = edram_rov_used ? 0 : uint32_t(mod.pixel.depth_stencil_mode);
  options.blend_rgb_factor = edram_rov_used ? XE_DXIL_BLEND_ONE : uint32_t(mod.pixel.rt0_blend_rgb_factor_for_premult);
  options.blend_alpha_factor = edram_rov_used ? XE_DXIL_BLEND_ONE : uint32_t(mod.pixel.rt0_blend_a_factor_for_premult);
  options.param_gen = mod.pixel.param_gen_enable;
  options.param_register = mod.pixel.param_gen_interpolator;
  options.param_point = mod.pixel.param_gen_point;
  options.scale_x = !edram_rov_used && mod.pixel.resolution_scale_native ? 1 : render_target_cache_.draw_resolution_scale_x();
  options.scale_y = !edram_rov_used && mod.pixel.resolution_scale_native ? 1 : render_target_cache_.draw_resolution_scale_y();
  options.native_2x = !edram_rov_used && render_target_cache_.msaa_2x_supported();
  options.precise_interpolation = precise_interpolation_supported() && !mod.pixel.param_gen_point;
  options.polygon_mask = SpirvShaderTranslator::kSysFlag_PrimitivePolygonal;
  options.line_mask = SpirvShaderTranslator::kSysFlag_PrimitiveLine;
  options.front_scale_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_front_scale) / 4;
  options.back_scale_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_back_scale) / 4;
  options.front_offset_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_front_offset) / 4;
  options.back_offset_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_poly_offset_back_offset) / 4;
  options.count_coverage = !edram_rov_used && zpd_hybrid_supported_ && mod.pixel.zpd_total();
  options.counter_word = offsetof(SpirvShaderTranslator::SystemConstants, zpd_fsi_counter_index) / 4;
  options.counter_stride_bytes = XenosZPDReport::kCounterSizeBytes;
  if (edram_rov_used) {
    options.rop = &rop_options;
    rop_options.color_mask = options.color_mask;
      rop_options.early_depth = !options.writes_depth;
    rop_options.no_blending = mod.pixel.fsi_no_blending();
    for (uint32_t rt = 0; rt < 4; ++rt) rop_options.formats[rt] = uint32_t(mod.pixel.fsi_rt_format(rt));
    rop_options.base_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_rt_base_dwords_scaled) / 4;
    rop_options.blend_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_rt_blend_factors_ops) / 4;
    rop_options.keep_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_rt_keep_mask) / 4;
    rop_options.clamp_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_rt_clamp) / 4;
    rop_options.blend_constant_word = options.blend_constant_word;
    rop_options.depth.msaa = uint32_t(mod.pixel.fsi_msaa_samples());
    rop_options.depth.scale_x = options.scale_x; rop_options.depth.scale_y = options.scale_y;
    rop_options.depth.full_counters = cvars::occlusion_query_full_counters;
    rop_options.depth.system_constants_bytes = options.system_constants_bytes;
    rop_options.depth.flags_word = options.flags_word;
    rop_options.depth.pitch_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_32bpp_tile_pitch_dwords_scaled) / 4;
    rop_options.depth.depth_base_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_depth_base_dwords_scaled) / 4;
    rop_options.depth.counter_word = options.counter_word;
    rop_options.depth.front_scale_word = options.front_scale_word; rop_options.depth.back_scale_word = options.back_scale_word;
    rop_options.depth.front_offset_word = options.front_offset_word; rop_options.depth.back_offset_word = options.back_offset_word;
    rop_options.depth.front_masks_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_front_reference_masks) / 4;
    rop_options.depth.back_masks_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_back_reference_masks) / 4;
    rop_options.depth.front_ops_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_front_func_ops) / 4;
    rop_options.depth.back_ops_word = offsetof(SpirvShaderTranslator::SystemConstants, edram_stencil_back_func_ops) / 4;
    rop_options.depth.depth_stencil_mask = SpirvShaderTranslator::kSysFlag_FSIDepthStencil;
    rop_options.depth.float24_mask = SpirvShaderTranslator::kSysFlag_DepthFloat24;
    rop_options.depth.depth_write_mask = SpirvShaderTranslator::kSysFlag_FSIDepthWrite;
    rop_options.depth.stencil_mask = SpirvShaderTranslator::kSysFlag_FSIStencilTest;
    rop_options.depth.depth_function_shift = SpirvShaderTranslator::kSysFlag_FSIDepthPassIfLess_Shift;
  }
  OwnedNativeDxil binary(xe_dxil_build_guest_pixel_owned(&options,
      shader.ucode_dwords(), shader.ucode_dword_count(), shader.cf_pair_index_bound()*2),
      &xe_dxil_shader_binary_delete);
  if (binary && !PublishNativeTextureLayout(*binding_shader,texture_layout)) return nullptr;
  bindings_lock.unlock();
  std::lock_guard<std::mutex> lock(native_dxil_cache_mutex_);
  auto& cache = native_pixel_dxil_cache_[shader.ucode_data_hash()];
  return cache.try_emplace(modification, std::move(binary)).first->second.get();
}
SpirvShader* PipelineCache::GetGuestShader(uint64_t ucode_hash) {
  auto it = shaders_.find(ucode_hash);
  if (it == shaders_.end()) {
    return nullptr;
  }
  // The bindings may still be translating on a creation thread (deferred pixel
  // shader). Only expose the shader once they are published, so draw-thread
  // readers (texture mask, bindless index buffers) never see partial bindings -
  // until then the draw uses the no-PS placeholder, which needs none.
  return it->second->bindings_ready() ? it->second : nullptr;
}

const XeDxilShaderBinary* PipelineCache::GetGuestNativeGeometryDxil(GeometryShaderKey key) {
  {
    std::lock_guard<std::mutex> lock(native_dxil_cache_mutex_);
    auto cached = native_geometry_dxil_cache_.find(key.key);
    if (cached != native_geometry_dxil_cache_.end()) return cached->second.get();
  }
  XeDxilGeometryOptions options = {};
  options.type = uint32_t(key.type);
  options.interpolators = key.interpolator_count;
  options.clip_planes = key.user_clip_plane_cull ? 0 : key.user_clip_plane_count;
  options.cull_planes = key.user_clip_plane_cull ? key.user_clip_plane_count : 0;
  options.vertex_kill_and = key.has_vertex_kill_and;
  options.point_size = key.has_point_size;
  options.point_coordinates = key.has_point_coordinates;
  options.point_diameter_word = offsetof(SpirvShaderTranslator::SystemConstants,point_constant_diameter)/4;
  options.point_radius_word = offsetof(SpirvShaderTranslator::SystemConstants,point_screen_diameter_to_ndc_radius)/4;
  OwnedNativeDxil binary(xe_dxil_build_geometry_owned(&options),&xe_dxil_shader_binary_delete);
  if (binary && !cvars::dump_shaders.empty()) {
    std::filesystem::path dir = std::filesystem::absolute(cvars::dump_shaders);
    std::filesystem::create_directories(dir);
    auto path = dir / fmt::format("shader_geometry_{:08X}.native_dxil.bin.geom",key.key);
    if (FILE* file = filesystem::OpenFile(path,"wb")) {
      fwrite(binary->data,1,binary->size,file); fclose(file);
    }
  }
  if (!binary) XELOGW("native_dxil: geometry generation failed (key {:08X})",key.key);
  std::lock_guard<std::mutex> lock(native_dxil_cache_mutex_);
  return native_geometry_dxil_cache_.try_emplace(key.key,std::move(binary)).first->second.get();
}

bool PipelineCache::BuildNativePipelineDxil(
    Shader::Translation* vertex_translation,
    Shader::Translation* pixel_translation,
    PipelineRuntimeDescription& runtime_description) {
  const PipelineDescription& description = runtime_description.description;
  SpirvShaderTranslator::Modification mod(description.vertex_shader_modification);
  runtime_description.native_vertex_dxil = GetGuestNativeVertexDxil(
      vertex_translation->shader(), vertex_translation->modification());
  if (!runtime_description.native_vertex_dxil) return false;
  if (Shader::IsHostVertexShaderTypeDomain(mod.vertex.host_vertex_shader_type) &&
      !NativeTessellationHosts(description.primitive_topology_type_or_tessellation_mode,
        mod.vertex.host_vertex_shader_type, runtime_description.native_tess_host_vs,
        runtime_description.native_tess_host_hs)) return false;
  runtime_description.root_signature = command_processor_.GetNativeRootSignature();
  if (pixel_translation) {
    runtime_description.native_pixel_dxil = GetGuestNativePixelDxil(
        pixel_translation->shader(), pixel_translation->modification());
    if (!runtime_description.native_pixel_dxil) return false;
  }
  if (description.geometry_shader != PipelineGeometryShader::kNone) {
    GeometryShaderKey key;
    if (!GetGeometryShaderKey(description.geometry_shader,description.vertex_shader_modification,
        description.pixel_shader_modification,key)) return false;
    runtime_description.native_geometry_dxil = GetGuestNativeGeometryDxil(key);
    if (!runtime_description.native_geometry_dxil) return false;
  }
  return true;
}
bool PipelineCache::ConfigurePipeline(
    Shader::Translation* vertex_shader, Shader::Translation* pixel_shader,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask, bool apply_polygon_offset_in_shader,
    bool zpd_total, bool viz_survey,
    uint32_t bound_depth_and_color_render_target_bits,
    const uint32_t* bound_depth_and_color_render_target_formats,
    bool use_interpreter, void** pipeline_handle_out,
    ID3D12RootSignature** root_signature_out) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  assert_not_null(pipeline_handle_out);
  assert_not_null(root_signature_out);

  // Ensure shaders are translated - needed now for GetCurrentStateDescription.
  // Edge flags are not supported yet (because polygon primitives are not).
  assert_true(register_file_.Get<reg::SQ_PROGRAM_CNTL>().vs_export_mode !=
                  xenos::VertexShaderExportMode::kPosition2VectorsEdge &&
              register_file_.Get<reg::SQ_PROGRAM_CNTL>().vs_export_mode !=
                  xenos::VertexShaderExportMode::kPosition2VectorsEdgeKill);
  assert_false(register_file_.Get<reg::SQ_PROGRAM_CNTL>().gen_index_vtx);

  // Check if we should use async pipeline creation.
  // When enabled, defer shader translation and pipeline creation to background.
  // Only use async when there's a pixel shader - VS-only pipelines are fast
  // to compile and don't benefit from async (vertex shaders are small).
  bool use_async = cvars::async_shader_compilation &&
                   creation_queue_.has_threads() && pixel_shader != nullptr;
  // An immediate hot-swap placeholder pipeline needs the real vertex shader and
  // a root signature that does not depend on the (not-yet-translated) pixel
  // shader. Only bindless mode has such a fixed root signature, so the
  // placeholder is limited to it. Bindful async still defers everything and the
  // draw is skipped until the real pipeline is ready.
  bool use_placeholder = use_async && bindless_resources_used_;

  // D3D12 produces plain vertex or tessellation domain host vertex shader
  // types. Point, rect and quad expansion is the built-in geometry shader. Both
  // are handled here. Anything else cannot render and the draw is skipped.
  Shader::HostVertexShaderType host_vs_type =
      SpirvShaderTranslator::Modification(vertex_shader->modification())
          .vertex.host_vertex_shader_type;
  if (host_vs_type != Shader::HostVertexShaderType::kVertex &&
      !Shader::IsHostVertexShaderTypeDomain(host_vs_type)) {
    XELOGE(
        "native_dxil: unsupported host vertex shader type {}; skipping draw",
        uint32_t(host_vs_type));
    return false;
  }

  // Ensure VS ucode is analyzed (needed for description hash).
  if (!vertex_shader->shader().is_ucode_analyzed()) {
    vertex_shader->shader().AnalyzeUcode(ucode_disasm_buffer_);
  }
  // Ensure PS ucode is analyzed too: UpdateBindingsNative reads its constant
  // register map (analysis-derived), and nothing else analyzes the pixel
  // shader ucode on this path.
  if (pixel_shader != nullptr && !pixel_shader->shader().is_ucode_analyzed()) {
    pixel_shader->shader().AnalyzeUcode(ucode_disasm_buffer_);
  }

  PipelineRuntimeDescription runtime_description;
  if (!GetCurrentStateDescription(
          vertex_shader, pixel_shader, primitive_processing_result,
          normalized_depth_control, normalized_color_mask,
          apply_polygon_offset_in_shader, zpd_total, viz_survey,
          bound_depth_and_color_render_target_bits,
          bound_depth_and_color_render_target_formats, runtime_description)) {
    return false;
  }
  PipelineDescription& description = runtime_description.description;

  // The ucode interpreter placeholder additionally defers the vertex shader:
  // instead of translating it here it rasterizes with the interpreter VS while
  // the creation thread builds both real shaders. Plain vertex shaders only
  // (the interpreter does not tessellate).
  bool make_interpreter = use_interpreter && use_placeholder &&
                          host_vs_type == Shader::HostVertexShaderType::kVertex &&
                          description.geometry_shader == PipelineGeometryShader::kNone;
  // A draw the interpreter can't stand in for whose VS isn't translated yet has
  // no placeholder. With async_shader_skip_draws, defer both shaders to the
  // creation thread and skip the draw until the real pipeline is ready, instead
  // of translating the VS inline on the draw thread.
  bool defer_without_placeholder = use_placeholder && !make_interpreter &&
                                   cvars::async_shader_skip_draws &&
                                   !IsGuestVertexReady(*vertex_shader);
  // Both cases build both shaders on the creation thread (the interpreter
  // behind a placeholder, the skip case behind nothing).
  bool defer_both = make_interpreter || defer_without_placeholder;

  // The canonical SPIR-V modifications, also used to key the SpirvShader DXIL.
  uint64_t vertex_spirv_modification = 0;
  uint64_t pixel_spirv_modification = 0;

  // Native vertex and host-target pixel compilation is attempted first.
  // Remaining paths use Native; geometry is generated directly as DXIL.
  // modification() is the canonical SPIR-V modification (computed by the
  // command processor). The guest shaders are translated with it directly.
  SpirvShaderTranslator::Modification vs_modification(
      vertex_shader->modification());
  // Tessellation: the guest vertex shader becomes the domain shader (the host
  // vertex/hull shaders are the precompiled tessellation_*_vs / *_hs).
  Shader::HostVertexShaderType host_vertex_shader_type =
      vs_modification.vertex.host_vertex_shader_type;
  bool is_tess_eval =
      Shader::IsHostVertexShaderTypeDomain(host_vertex_shader_type);
  vertex_spirv_modification = vertex_shader->modification();
  Shader::Translation* pixel_ps_translation = nullptr;
  if (pixel_shader != nullptr) {
    pixel_spirv_modification = pixel_shader->modification();
    // Create the PS metadata handle now. Native DXIL emission happens on a
    // creation thread (async) or just below (sync).
    pixel_ps_translation = EnsureGuestTranslation(
        static_cast<SpirvShader&>(pixel_shader->shader()),
        pixel_spirv_modification);
  }
  // Persist the guest ucode so the storage reload can rebuild these Native
  // shaders (translated at the then-current resolution) before first draw.
  if (storage_writer_.is_active()) {
    if (vertex_shader->shader().try_set_ucode_storage_index(
            storage_writer_.storage_index())) {
      shader_storage_file_flush_needed_ = true;
      storage_writer_.QueueShaderWrite(&vertex_shader->shader());
    }
    if (pixel_shader != nullptr &&
        pixel_shader->shader().try_set_ucode_storage_index(
            storage_writer_.storage_index())) {
      shader_storage_file_flush_needed_ = true;
      storage_writer_.QueueShaderWrite(&pixel_shader->shader());
    }
  }
  runtime_description.root_signature =
      command_processor_.GetNativeRootSignature();

  if (defer_both) {
    // Defer BOTH shaders to the creation thread. Create the (untranslated)
    // translation objects; the creation thread builds their DXIL via the
    // BuildNativePipelineDxil warm-up branch (keyed on native_vertex_translation).
    // The interpreter rasterizes via a placeholder meanwhile; the skip case has
    // no placeholder and the draw is dropped until the real pipeline is ready.
    runtime_description.native_vertex_translation =
        EnsureGuestTranslation(
            static_cast<SpirvShader&>(vertex_shader->shader()),
            vertex_spirv_modification);
    runtime_description.native_pixel_translation = pixel_ps_translation;
    if (!runtime_description.native_vertex_translation) {
      return false;
    }
  } else {
    // Build the vertex shader for the live draw and its placeholder. Native
    // generation needs no intermediate IR; native generation publishes texture
    // bindings. Pixel conversion and PSO creation may run asynchronously.
    runtime_description.native_vertex_dxil = GetGuestNativeVertexDxil(
        vertex_shader->shader(), vertex_spirv_modification);
    if (!runtime_description.native_vertex_dxil) {
      XELOGE("native_dxil: guest vertex/domain generation failed; skipping draw");
      return false;
    }
    if (is_tess_eval && !NativeTessellationHosts(
        description.primitive_topology_type_or_tessellation_mode,host_vertex_shader_type,
        runtime_description.native_tess_host_vs,runtime_description.native_tess_host_hs)) return false;
    if (pixel_shader == nullptr) {
      // Depth-only: CreateD3D12Pipeline supplies native depth-only DXIL
      // (ROV path) or no pixel shader (host render target path). No pixel DXIL
      // translated here.
    } else if (use_async) {
      // Defer native pixel shader generation and PSO creation to
      // a creation thread.
      runtime_description.native_pixel_translation = pixel_ps_translation;
    } else {
      runtime_description.native_pixel_dxil = GetGuestNativePixelDxil(
          pixel_shader->shader(), pixel_spirv_modification);
      if (!runtime_description.native_pixel_dxil) {
        XELOGE(
            "native_dxil: guest pixel shader {:016X} translation failed; "
            "skipping draw",
            pixel_shader->shader().ucode_data_hash());
        return false;
      }
    }
  }
  // Built-in geometry shader (point/rect/quad expansion). Produced now on the
  // main thread so the placeholder pipeline can rasterize the expanded
  // primitive.
  if (description.geometry_shader != PipelineGeometryShader::kNone) {
    GeometryShaderKey gs_key;
    if (GetGeometryShaderKey(
            description.geometry_shader, vertex_shader->modification(),
            pixel_shader ? pixel_shader->modification() : uint64_t(0),
            gs_key)) {
      runtime_description.native_geometry_dxil = GetGuestNativeGeometryDxil(gs_key);
    }
    if (!runtime_description.native_geometry_dxil) {
      XELOGE(
          "native_dxil: built-in geometry shader translation failed; "
          "skipping draw");
      return false;
    }
  }

  if (current_pipeline_ != nullptr &&
      current_pipeline_->description.description == description &&
      !current_pipeline_->wants_rebuild()) {
    *pipeline_handle_out = current_pipeline_;
    *root_signature_out = current_pipeline_->description.root_signature;
    return true;
  }

  // Find an existing pipeline in the cache.
  uint64_t hash = XXH3_64bits(&description, sizeof(description));
  // A warm-up entry whose creation failed is rebuilt below instead of returned.
  // Only this thread touches the map, so creation threads can flag but not act.
  Pipeline* rebuild_pipeline = nullptr;
  auto found_range = pipelines_.equal_range(hash);
  for (auto it = found_range.first; it != found_range.second; ++it) {
    Pipeline* found_pipeline = it->second;
    if (found_pipeline->description.description == description) {
      if (found_pipeline->wants_rebuild()) {
        rebuild_pipeline = found_pipeline;
        break;
      }
      current_pipeline_ = found_pipeline;
      *pipeline_handle_out = found_pipeline;
      *root_signature_out = found_pipeline->description.root_signature;
      return true;
    }
  }

  Pipeline* new_pipeline = rebuild_pipeline;
  if (new_pipeline) {
    // Already keyed under this hash. The description built above replaces the
    // one whose DXIL never arrived, so the rebuild uses live state.
    XELOGW("Rebuilding pipeline that failed to create (VS {:016X}, PS {:016X})",
           vertex_shader->shader().ucode_data_hash(),
           pixel_shader ? pixel_shader->shader().ucode_data_hash() : 0);
    // A live build from here on, so a second failure is final.
    new_pipeline->from_storage = false;
  } else {
    new_pipeline = new Pipeline;
    pipelines_.emplace(hash, new_pipeline);
    COUNT_profile_set("gpu/pipeline_cache/pipelines", pipelines_.size());
  }
  std::memcpy(&new_pipeline->description, &runtime_description,
              sizeof(runtime_description));

  if (use_async) {
    // Queue for background thread. A real-VS placeholder has its VS translated
    // already so only the PS is left pending; the interpreter and skip cases
    // defer both via native_*_translation (BuildNativePipelineDxil warm-up branch).
    new_pipeline->pending_vertex_shader =
        (use_placeholder && !defer_both) ? nullptr : vertex_shader;
    new_pipeline->pending_pixel_shader = pixel_shader;
    // The skip case (defer_without_placeholder) intentionally creates no
    // placeholder - state stays null and the draw is dropped until ready.
    if (use_placeholder && !defer_without_placeholder) {
      // Create a placeholder pipeline now so the draw can proceed immediately -
      // real VS + no-op PS, or (interpreter) the ucode interpreter VS + no-op/
      // debug PS with both real shaders deferred. The creation thread swaps in
      // the real pipeline when translation finishes.
      ID3D12PipelineState* placeholder_state = CreateD3D12Pipeline(
          runtime_description, make_interpreter
                                   ? PipelinePlaceholderMode::kInterpreter
                                   : PipelinePlaceholderMode::kRealVertex);
      // Published before state so a draw that loads the placeholder out of
      // state always sees it here too, and pins this concrete PSO rather than
      // the swappable handle - the real pipeline must never run against
      // bindings built for the placeholder.
      new_pipeline->placeholder_state.store(placeholder_state,
                                            std::memory_order_release);
      new_pipeline->uses_interpreter.store(
          make_interpreter && placeholder_state != nullptr,
          std::memory_order_release);
      new_pipeline->state.store(placeholder_state, std::memory_order_release);
      new_pipeline->is_placeholder.store(placeholder_state != nullptr,
                                         std::memory_order_release);
      if (make_interpreter && placeholder_state != nullptr) {
        XELOGI(
            "VS interpreter placeholder created (interpreter VS + no-op PS): "
            "VS {:016X}, PS {:016X}",
            vertex_shader->shader().ucode_data_hash(),
            pixel_shader ? pixel_shader->shader().ucode_data_hash() : 0);
      }
    }
    new_pipeline->creation_pending.store(true, std::memory_order_relaxed);
    creation_queue_.Push(new_pipeline);
  } else {
    // Sync mode or no creation threads: create synchronously.
    new_pipeline->state.store(CreateD3D12Pipeline(runtime_description),
                              std::memory_order_release);
  }

  // Native pipelines persist too: their SPIR-V modifications are stored so the
  // reload can rebuild the DXIL (translated at the then-current resolution).
  // A rebuilt entry came from storage, so writing it again would duplicate it.
  if (storage_writer_.is_active() && !rebuild_pipeline) {
    pipeline_storage_file_flush_needed_ = true;
    storage_writer_.QueuePipelineWrite(hash, &description);
  }

  current_pipeline_ = new_pipeline;
  *pipeline_handle_out = new_pipeline;
  *root_signature_out = runtime_description.root_signature;
  return true;
}

void PipelineCache::AnalyzeShadersForStorage(
    const std::set<std::pair<uint64_t, uint64_t>>& translations_needed) {
  uint64_t analysis_start = xe::Clock::QueryHostTickCount();

  // Only shaders referenced by a stored pipeline need analyzing. Analysis is
  // per ucode, not per translation, so the modification in the pair is ignored
  // here.
  std::vector<SpirvShader*> shaders_to_analyze;
  shaders_to_analyze.reserve(shaders_.size());
  for (auto& shader_pair : shaders_) {
    SpirvShader* shader = shader_pair.second;
    uint64_t ucode_data_hash = shader->ucode_data_hash();
    if (translations_needed.lower_bound(
            std::make_pair(ucode_data_hash, uint64_t(0))) !=
        translations_needed.upper_bound(
            std::make_pair(ucode_data_hash, UINT64_MAX))) {
      shaders_to_analyze.push_back(shader);
    }
  }

  if (shaders_to_analyze.empty()) {
    return;
  }

  std::atomic<size_t> analysis_index{0};
  auto analyze_function = [&]() {
    StringBuffer ucode_disasm_buffer;
    while (true) {
      size_t index = analysis_index.fetch_add(1);
      if (index >= shaders_to_analyze.size()) {
        break;
      }
      SpirvShader* shader = shaders_to_analyze[index];
      if (!shader->is_ucode_analyzed()) {
        shader->AnalyzeUcode(ucode_disasm_buffer);
      }
    }
  };

  size_t logical_processor_count = xe::threading::logical_processor_count();
  if (!logical_processor_count) {
    logical_processor_count = 6;
  }
  size_t thread_count =
      std::min(logical_processor_count - size_t(1), shaders_to_analyze.size());
  std::vector<std::unique_ptr<xe::threading::Thread>> analysis_threads;
  for (size_t i = 0; i < thread_count; ++i) {
    auto thread = xe::threading::Thread::Create({}, analyze_function);
    if (thread) {
      thread->set_name("Shader Analysis");
      analysis_threads.push_back(std::move(thread));
    }
  }

  // Main thread also participates.
  analyze_function();

  for (auto& thread : analysis_threads) {
    xe::threading::Wait(thread.get(), false);
  }

  XELOGI("Analyzed {} shaders in {} ms", shaders_to_analyze.size(),
         (xe::Clock::QueryHostTickCount() - analysis_start) * 1000 /
             xe::Clock::QueryHostTickFrequency());
}

bool PipelineCache::GetCurrentStateDescription(
    Shader::Translation* vertex_shader, Shader::Translation* pixel_shader,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask, bool depth_bias_in_pixel_shader,
    bool zpd_total, bool viz_survey,
    uint32_t bound_depth_and_color_render_target_bits,
    const uint32_t* bound_depth_and_color_render_target_formats,
    PipelineRuntimeDescription& runtime_description_out) {
  PipelineDescription& description_out = runtime_description_out.description;

  const auto& regs = register_file_;
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();

  // Initialize all unused fields to zero for comparison/hashing.
  std::memset(&runtime_description_out, 0, sizeof(runtime_description_out));

  assert_true(SpirvShaderTranslator::Modification(vertex_shader->modification())
                  .vertex.host_vertex_shader_type ==
              primitive_processing_result.host_vertex_shader_type);
  bool tessellated = primitive_processing_result.IsTessellated();
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);
  bool rasterization_enabled =
      draw_util::IsRasterizationPotentiallyDone(regs, primitive_polygonal);
  // In Direct3D, rasterization (along with pixel counting) is disabled by
  // disabling the pixel shader and depth / stencil. However, if rasterization
  // should be disabled, the pixel shader must be disabled externally, to ensure
  // things like texture binding layout is correct for the shader actually being
  // used (don't replace anything here).
  if (!rasterization_enabled) {
    assert_null(pixel_shader);
    if (pixel_shader) {
      return false;
    }
  }

  bool edram_rov_used = render_target_cache_.GetPath() ==
                        RenderTargetCache::Path::kPixelShaderInterlock;

  // Root signature. Guest DXIL uses the fixed native
  // root signature (ConfigurePipeline sets it again once committed to the
  // path).
  runtime_description_out.root_signature =
      command_processor_.GetNativeRootSignature();
  if (runtime_description_out.root_signature == nullptr) {
    return false;
  }

  // Vertex shader.
  runtime_description_out.vertex_shader = vertex_shader;
  description_out.vertex_shader_hash =
      vertex_shader->shader().ucode_data_hash();
  description_out.vertex_shader_modification = vertex_shader->modification();

  // Index buffer strip cut value.
  if (primitive_processing_result.host_primitive_reset_enabled) {
    description_out.strip_cut_index =
        primitive_processing_result.host_index_format ==
                xenos::IndexFormat::kInt16
            ? PipelineStripCutIndex::kFFFF
            : PipelineStripCutIndex::kFFFFFFFF;
  } else {
    description_out.strip_cut_index = PipelineStripCutIndex::kNone;
  }

  // Host vertex shader type and primitive topology.
  if (tessellated) {
    description_out.primitive_topology_type_or_tessellation_mode =
        uint32_t(primitive_processing_result.tessellation_mode);
  } else {
    switch (primitive_processing_result.host_primitive_type) {
      case xenos::PrimitiveType::kPointList:
        description_out.primitive_topology_type_or_tessellation_mode =
            uint32_t(PipelinePrimitiveTopologyType::kPoint);
        break;
      case xenos::PrimitiveType::kLineList:
      case xenos::PrimitiveType::kLineStrip:
      // Quads are emulated as line lists with adjacency.
      case xenos::PrimitiveType::kQuadList:
      case xenos::PrimitiveType::k2DLineStrip:
        description_out.primitive_topology_type_or_tessellation_mode =
            uint32_t(PipelinePrimitiveTopologyType::kLine);
        break;
      default:
        description_out.primitive_topology_type_or_tessellation_mode =
            uint32_t(PipelinePrimitiveTopologyType::kTriangle);
        break;
    }
    switch (primitive_processing_result.host_primitive_type) {
      case xenos::PrimitiveType::kPointList:
        description_out.geometry_shader = PipelineGeometryShader::kPointList;
        break;
      case xenos::PrimitiveType::kRectangleList:
        description_out.geometry_shader =
            PipelineGeometryShader::kRectangleList;
        break;
      case xenos::PrimitiveType::kQuadList:
        description_out.geometry_shader = PipelineGeometryShader::kQuadList;
        break;
      case xenos::PrimitiveType::kLineList:
      case xenos::PrimitiveType::kLineStrip:
        // Host lines are 1 host pixel wide, expand them to 1 guest pixel when
        // the draw is resolution-scaled.
        description_out.geometry_shader =
            (render_target_cache_.GetDrawScaleX() > 1 ||
             render_target_cache_.GetDrawScaleY() > 1)
                ? PipelineGeometryShader::kLineList
                : PipelineGeometryShader::kNone;
        break;
      default:
        description_out.geometry_shader = PipelineGeometryShader::kNone;
        break;
    }
  }
  // The rest doesn't matter when rasterization is disabled (thus no writing to
  // anywhere from post-geometry stages and no samples are counted).
  if (!rasterization_enabled) {
    description_out.cull_mode = PipelineCullMode::kDisableRasterization;
    return true;
  }

  // Pixel shader.
  if (pixel_shader) {
    runtime_description_out.pixel_shader = pixel_shader;
    description_out.pixel_shader_hash =
        pixel_shader->shader().ucode_data_hash();
    description_out.pixel_shader_modification = pixel_shader->modification();
  }

  // Rasterizer state.
  // Because Direct3D 12 doesn't support per-side fill mode and depth bias, the
  // values to use depends on the current culling state.
  // If front faces are culled, use the ones for back faces.
  // If back faces are culled, it's the other way around.
  // If culling is not enabled, assume the developer wanted to draw things in a
  // more special way - so if one side is wireframe or has a depth bias, then
  // that's intentional (if both sides have a depth bias, the one for the front
  // faces is used, though it's unlikely that they will ever be different -
  // SetRenderState sets the same offset for both sides).
  // Points fill mode (0) also isn't supported in Direct3D 12, but assume the
  // developer didn't want to fill the whole primitive and use wireframe (like
  // Xenos fill mode 1).
  // Here we also assume that only one side is culled - if two sides are culled,
  // rasterization will be disabled externally, or the draw call will be dropped
  // early if the vertex shader doesn't export to memory.
  bool cull_front, cull_back;
  if (primitive_polygonal) {
    description_out.front_counter_clockwise = pa_su_sc_mode_cntl.face == 0;
    cull_front = pa_su_sc_mode_cntl.cull_front != 0;
    cull_back = pa_su_sc_mode_cntl.cull_back != 0;
    if (cull_front) {
      // The case when both faces are culled should be handled by disabling
      // rasterization.
      assert_false(cull_back);
      description_out.cull_mode = PipelineCullMode::kFront;
    } else if (cull_back) {
      description_out.cull_mode = PipelineCullMode::kBack;
    } else {
      description_out.cull_mode = PipelineCullMode::kNone;
    }
    // With ROV, the depth bias is applied in the pixel shader because
    // per-sample depth is needed for MSAA.
    if (!cull_front) {
      // Front faces aren't culled.
      // Direct3D 12, unfortunately, doesn't support point fill mode.
      if (pa_su_sc_mode_cntl.polymode_front_ptype !=
          xenos::PolygonType::kTriangles) {
        description_out.fill_mode_wireframe = 1;
      }
    }
    if (!cull_back) {
      // Back faces aren't culled.
      if (pa_su_sc_mode_cntl.polymode_back_ptype !=
          xenos::PolygonType::kTriangles) {
        description_out.fill_mode_wireframe = 1;
      }
    }
    if (pa_su_sc_mode_cntl.poly_mode != xenos::PolygonModeEnable::kDualMode) {
      description_out.fill_mode_wireframe = 0;
    }
  } else {
    // Filled front faces only, without culling.
    cull_front = false;
    cull_back = false;
  }
  if (!edram_rov_used && !depth_bias_in_pixel_shader) {
    float polygon_offset, polygon_offset_scale;
    draw_util::GetPreferredFacePolygonOffset(
        regs, primitive_polygonal, polygon_offset_scale, polygon_offset);
    description_out.depth_bias = draw_util::GetD3D10IntegerPolygonOffset(
        regs.Get<reg::RB_DEPTH_INFO>().depth_format, polygon_offset);
    description_out.depth_bias_slope_scaled =
        polygon_offset_scale * xenos::kPolygonOffsetScaleSubpixelUnit;
    // The slope-scaled depth bias multiplier depends on this at pipeline
    // creation.
    description_out.resolution_scale_native =
        uint32_t(render_target_cache_.IsDrawScaleNative());
  }
  description_out.counting_depth_only =
      uint32_t(edram_rov_used ? viz_survey : zpd_total);
  if (tessellated && cvars::d3d12_tessellation_wireframe) {
    description_out.fill_mode_wireframe = 1;
  }

  // With force_depth_clamp, use the host viewport clamp instead of near and far
  // Z plane clipping. X/Y/W clipping is unchanged. Both 494707EE and 41560881
  // have passes that rely on alpha inputs that currently gets dropped by
  // near-plane clipping.
  // TODO(boma): Investigate whether the difference is in shader arithmetic or
  // the clipper itself.
  description_out.depth_clip = !regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable &&
                               !cvars::force_depth_clamp;
  bool depth_stencil_bound_and_used = false;
  if (!edram_rov_used) {
    // Depth/stencil. No stencil, always passing depth test and no depth writing
    // means depth disabled.
    if (bound_depth_and_color_render_target_bits & 1) {
      if (normalized_depth_control.z_enable) {
        description_out.depth_func = normalized_depth_control.zfunc;
        description_out.depth_write = normalized_depth_control.z_write_enable;
      } else {
        description_out.depth_func = xenos::CompareFunction::kAlways;
      }
      if (normalized_depth_control.stencil_enable) {
        description_out.stencil_enable = 1;
        bool stencil_backface_enable =
            primitive_polygonal && normalized_depth_control.backface_enable;
        // Per-face masks not supported by Direct3D 12, choose the back face
        // ones only if drawing only back faces.
        Register stencil_ref_mask_reg;
        if (stencil_backface_enable && cull_front) {
          stencil_ref_mask_reg = XE_GPU_REG_RB_STENCILREFMASK_BF;
        } else {
          stencil_ref_mask_reg = XE_GPU_REG_RB_STENCILREFMASK;
        }
        auto stencil_ref_mask =
            regs.Get<reg::RB_STENCILREFMASK>(stencil_ref_mask_reg);
        description_out.stencil_read_mask = stencil_ref_mask.stencilmask;
        description_out.stencil_write_mask = stencil_ref_mask.stencilwritemask;
        description_out.stencil_front_fail_op =
            normalized_depth_control.stencilfail;
        description_out.stencil_front_depth_fail_op =
            normalized_depth_control.stencilzfail;
        description_out.stencil_front_pass_op =
            normalized_depth_control.stencilzpass;
        description_out.stencil_front_func =
            normalized_depth_control.stencilfunc;
        if (stencil_backface_enable) {
          description_out.stencil_back_fail_op =
              normalized_depth_control.stencilfail_bf;
          description_out.stencil_back_depth_fail_op =
              normalized_depth_control.stencilzfail_bf;
          description_out.stencil_back_pass_op =
              normalized_depth_control.stencilzpass_bf;
          description_out.stencil_back_func =
              normalized_depth_control.stencilfunc_bf;
        } else {
          description_out.stencil_back_fail_op =
              description_out.stencil_front_fail_op;
          description_out.stencil_back_depth_fail_op =
              description_out.stencil_front_depth_fail_op;
          description_out.stencil_back_pass_op =
              description_out.stencil_front_pass_op;
          description_out.stencil_back_func =
              description_out.stencil_front_func;
        }
      }
      // If not binding the DSV, ignore the format in the hash.
      if (description_out.depth_func != xenos::CompareFunction::kAlways ||
          description_out.depth_write || description_out.stencil_enable) {
        description_out.depth_format = xenos::DepthRenderTargetFormat(
            bound_depth_and_color_render_target_formats[0]);
        depth_stencil_bound_and_used = true;
      }
    } else {
      description_out.depth_func = xenos::CompareFunction::kAlways;
    }

    // Render targets and blending state. 32 because of 0x1F mask, for safety
    // (all unknown to zero).
    static constexpr PipelineBlendFactor kBlendFactorMap[32] = {
        /*  0 */ PipelineBlendFactor::kZero,
        /*  1 */ PipelineBlendFactor::kOne,
        /*  2 */ PipelineBlendFactor::kZero,  // ?
        /*  3 */ PipelineBlendFactor::kZero,  // ?
        /*  4 */ PipelineBlendFactor::kSrcColor,
        /*  5 */ PipelineBlendFactor::kInvSrcColor,
        /*  6 */ PipelineBlendFactor::kSrcAlpha,
        /*  7 */ PipelineBlendFactor::kInvSrcAlpha,
        /*  8 */ PipelineBlendFactor::kDestColor,
        /*  9 */ PipelineBlendFactor::kInvDestColor,
        /* 10 */ PipelineBlendFactor::kDestAlpha,
        /* 11 */ PipelineBlendFactor::kInvDestAlpha,
        // CONSTANT_COLOR
        /* 12 */ PipelineBlendFactor::kBlendFactor,
        // ONE_MINUS_CONSTANT_COLOR
        /* 13 */ PipelineBlendFactor::kInvBlendFactor,
        // CONSTANT_ALPHA
        /* 14 */ PipelineBlendFactor::kAlphaFactor,
        // ONE_MINUS_CONSTANT_ALPHA
        /* 15 */ PipelineBlendFactor::kInvAlphaFactor,
        /* 16 */ PipelineBlendFactor::kSrcAlphaSat,
    };
    // Like kBlendFactorMap, but with color modes changed to alpha. Some
    // pipelines aren't created in 545407E0 because a color mode is used for
    // alpha.
    static constexpr PipelineBlendFactor kBlendFactorAlphaMap[32] = {
        /*  0 */ PipelineBlendFactor::kZero,
        /*  1 */ PipelineBlendFactor::kOne,
        /*  2 */ PipelineBlendFactor::kZero,  // ?
        /*  3 */ PipelineBlendFactor::kZero,  // ?
        /*  4 */ PipelineBlendFactor::kSrcAlpha,
        /*  5 */ PipelineBlendFactor::kInvSrcAlpha,
        /*  6 */ PipelineBlendFactor::kSrcAlpha,
        /*  7 */ PipelineBlendFactor::kInvSrcAlpha,
        /*  8 */ PipelineBlendFactor::kDestAlpha,
        /*  9 */ PipelineBlendFactor::kInvDestAlpha,
        /* 10 */ PipelineBlendFactor::kDestAlpha,
        /* 11 */ PipelineBlendFactor::kInvDestAlpha,
        /* 12 */ PipelineBlendFactor::kBlendFactor,
        // ONE_MINUS_CONSTANT_COLOR
        /* 13 */ PipelineBlendFactor::kInvBlendFactor,
        // CONSTANT_ALPHA
        /* 14 */ PipelineBlendFactor::kBlendFactor,
        // ONE_MINUS_CONSTANT_ALPHA
        /* 15 */ PipelineBlendFactor::kInvBlendFactor,
        /* 16 */ PipelineBlendFactor::kSrcAlphaSat,
    };
    // While it's okay to specify fewer render targets in the pipeline state
    // (even fewer than written by the shader) than actually bound to the
    // command list (though this kind of truncation may only happen at the end -
    // DXGI_FORMAT_UNKNOWN *requires* a null RTV descriptor to be bound), not
    // doing that because sample counts of all render targets bound via
    // OMSetRenderTargets, even those beyond NumRenderTargets, apparently must
    // have their sample count matching the one set in the pipeline - however if
    // we set NumRenderTargets to 0 and also disable depth / stencil, the sample
    // count must be set to 1 - while the command list may still have
    // multisampled render targets bound (happens in 4D5307E6 main menu).
    // TODO(Triang3l): Investigate interaction of OMSetRenderTargets with
    // non-null depth and DSVFormat DXGI_FORMAT_UNKNOWN in the same case.
    for (uint32_t i = 0; i < 4; ++i) {
      if (!(bound_depth_and_color_render_target_bits &
            (uint32_t(1) << (1 + i)))) {
        continue;
      }
      PipelineRenderTarget& rt = description_out.render_targets[i];
      rt.used = 1;
      auto color_info = regs.Get<reg::RB_COLOR_INFO>(
          reg::RB_COLOR_INFO::rt_register_indices[i]);
      rt.format = xenos::ColorRenderTargetFormat(
          bound_depth_and_color_render_target_formats[1 + i]);
      rt.write_mask = (normalized_color_mask >> (i * 4)) & 0xF;
      if (rt.write_mask) {
        auto blendcontrol = regs.Get<reg::RB_BLENDCONTROL>(
            reg::RB_BLENDCONTROL::rt_register_indices[i]);
        rt.src_blend = kBlendFactorMap[uint32_t(blendcontrol.color_srcblend)];
        rt.dest_blend = kBlendFactorMap[uint32_t(blendcontrol.color_destblend)];
        rt.blend_op = blendcontrol.color_comb_fcn;
        rt.src_blend_alpha =
            kBlendFactorAlphaMap[uint32_t(blendcontrol.alpha_srcblend)];
        rt.dest_blend_alpha =
            kBlendFactorAlphaMap[uint32_t(blendcontrol.alpha_destblend)];
        rt.blend_op_alpha = blendcontrol.alpha_comb_fcn;
      } else {
        rt.src_blend = PipelineBlendFactor::kOne;
        rt.dest_blend = PipelineBlendFactor::kZero;
        rt.blend_op = xenos::BlendOp::kAdd;
        rt.src_blend_alpha = PipelineBlendFactor::kOne;
        rt.dest_blend_alpha = PipelineBlendFactor::kZero;
        rt.blend_op_alpha = xenos::BlendOp::kAdd;
      }
    }
  }
  xenos::MsaaSamples host_msaa_samples =
      regs.Get<reg::RB_SURFACE_INFO>().msaa_samples;
  if (edram_rov_used) {
    // The depth-only pixel shader is specialized for the guest count, which
    // the host one is about to lose.
    description_out.guest_msaa_samples = host_msaa_samples;
    if (host_msaa_samples == xenos::MsaaSamples::k2X) {
      // 2 is not supported in ForcedSampleCount on Nvidia.
      host_msaa_samples = xenos::MsaaSamples::k4X;
    }
  } else {
    if (!(bound_depth_and_color_render_target_bits & ~uint32_t(1)) &&
        !depth_stencil_bound_and_used) {
      // Direct3D 12 requires the sample count to be 1 when no color or depth /
      // stencil render targets are bound.
      // FIXME(Triang3l): Use ForcedSampleCount or some other fallback for
      // sample counting when needed, though with 2x it will be as incorrect as
      // with 1x / 4x anyway. Or bind a dummy depth / stencil buffer if really
      // needed.
      host_msaa_samples = xenos::MsaaSamples::k1X;
    }
    // TODO(Triang3l): 4x MSAA fallback when 2x isn't supported.
  }
  description_out.host_msaa_samples = host_msaa_samples;

  return true;
}

bool PipelineCache::GetGeometryShaderKey(
    PipelineGeometryShader geometry_shader_type,
    uint64_t vertex_shader_modification_value,
    uint64_t pixel_shader_modification_value, GeometryShaderKey& key_out) {
  if (geometry_shader_type == PipelineGeometryShader::kNone) {
    return false;
  }
  SpirvShaderTranslator::Modification vertex_shader_modification(
      vertex_shader_modification_value);
  SpirvShaderTranslator::Modification pixel_shader_modification(
      pixel_shader_modification_value);
  assert_true(vertex_shader_modification.vertex.interpolator_mask ==
              pixel_shader_modification.pixel.interpolator_mask);
  GeometryShaderKey key;
  key.type = geometry_shader_type;
  key.interpolator_count =
      xe::bit_count(vertex_shader_modification.vertex.interpolator_mask);
  key.user_clip_plane_count =
      vertex_shader_modification.vertex.user_clip_plane_count;
  key.user_clip_plane_cull =
      vertex_shader_modification.vertex.user_clip_plane_cull;
  key.has_vertex_kill_and = vertex_shader_modification.vertex.vertex_kill_and;
  key.has_point_size =
      vertex_shader_modification.vertex.output_point_parameters;
  key.has_point_coordinates = pixel_shader_modification.pixel.param_gen_point;
  key_out = key;
  return true;
}

void PipelineCache::EnsurePipelineShadersTranslated(Pipeline* pipeline) {
  PipelineRuntimeDescription& desc = pipeline->description;
  if (desc.native_vertex_translation && !desc.native_vertex_dxil) {
    BuildNativePipelineDxil(desc.native_vertex_translation,desc.native_pixel_translation,desc);
  } else if (pipeline->pending_pixel_shader && desc.native_pixel_translation && !desc.native_pixel_dxil) {
    desc.native_pixel_dxil = GetGuestNativePixelDxil(
        desc.native_pixel_translation->shader(),desc.native_pixel_translation->modification());
  }
  pipeline->pending_pixel_shader = nullptr;
  pipeline->pending_vertex_shader = nullptr;
}
ID3D12PipelineState* PipelineCache::CreateD3D12Pipeline(
    const PipelineRuntimeDescription& runtime_description,
    PipelinePlaceholderMode placeholder_mode) {
  const PipelineDescription& description = runtime_description.description;
  bool as_interpreter =
      placeholder_mode == PipelinePlaceholderMode::kInterpreter;
  // Both placeholder variants substitute the no-op (or debug) pixel shader.
  bool as_placeholder = placeholder_mode != PipelinePlaceholderMode::kNone;

  if (runtime_description.pixel_shader != nullptr) {
    XELOGGPU("Creating graphics pipeline with VS {:016X}, PS {:016X}",
             runtime_description.vertex_shader->shader().ucode_data_hash(),
             runtime_description.pixel_shader->shader().ucode_data_hash());
  } else {
    XELOGGPU("Creating graphics pipeline with VS {:016X}",
             runtime_description.vertex_shader->shader().ucode_data_hash());
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC state_desc;
  std::memset(&state_desc, 0, sizeof(state_desc));

  bool edram_rov_used = render_target_cache_.GetPath() ==
                        RenderTargetCache::Path::kPixelShaderInterlock;

  // Root signature.
  state_desc.pRootSignature = runtime_description.root_signature;

  // Index buffer strip cut value.
  switch (description.strip_cut_index) {
    case PipelineStripCutIndex::kFFFF:
      state_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
      break;
    case PipelineStripCutIndex::kFFFFFFFF:
      state_desc.IBStripCutValue =
          D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
      break;
    default:
      state_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
      break;
  }

  // Primitive topology, vertex, hull, domain and geometry shaders.
  // Bytecode comes from owned native containers, so no separate translation
  // is required.
  Shader::HostVertexShaderType host_vertex_shader_type =
      SpirvShaderTranslator::Modification(
          runtime_description.vertex_shader->modification())
          .vertex.host_vertex_shader_type;
  if (Shader::IsHostVertexShaderTypeDomain(host_vertex_shader_type)) {
    state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    xenos::TessellationMode tessellation_mode = xenos::TessellationMode(
        description.primitive_topology_type_or_tessellation_mode);
    if (runtime_description.native_vertex_dxil) {
      state_desc.VS = runtime_description.native_tess_host_vs;
      state_desc.HS = runtime_description.native_tess_host_hs;
      state_desc.DS.pShaderBytecode = runtime_description.native_vertex_dxil->data;
      state_desc.DS.BytecodeLength = runtime_description.native_vertex_dxil->size;
    }
    if (runtime_description.native_vertex_dxil) {
      if (!state_desc.VS.pShaderBytecode || !state_desc.HS.pShaderBytecode) return nullptr;
    } else {
      // Native generation produced no DXIL. Skip the
      // draw.
      return nullptr;
    }
  } else {
    assert_true(host_vertex_shader_type ==
                Shader::HostVertexShaderType::kVertex);
    if (host_vertex_shader_type != Shader::HostVertexShaderType::kVertex) {
      // Fallback vertex shaders are not needed on Direct3D 12.
      return nullptr;
    }
    if (as_interpreter) {
      // Interpreter placeholder: the guest VS is deferred, so rasterize with
      // the ucode interpreter VS (reads the guest ucode from shared memory).
      state_desc.VS.pShaderBytecode = shaders::ucode_interpreter_vs;
      state_desc.VS.BytecodeLength = sizeof(shaders::ucode_interpreter_vs);
    } else if (runtime_description.native_vertex_dxil) {
      state_desc.VS.pShaderBytecode = runtime_description.native_vertex_dxil->data;
      state_desc.VS.BytecodeLength = runtime_description.native_vertex_dxil->size;
    } else {
      // Native generation produced no DXIL. Skip the
      // draw.
      return nullptr;
    }
    PipelinePrimitiveTopologyType primitive_topology_type =
        PipelinePrimitiveTopologyType(
            description.primitive_topology_type_or_tessellation_mode);
    switch (primitive_topology_type) {
      case PipelinePrimitiveTopologyType::kPoint:
        state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        break;
      case PipelinePrimitiveTopologyType::kLine:
        state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        break;
      case PipelinePrimitiveTopologyType::kTriangle:
        state_desc.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        break;
      default:
        assert_unhandled_case(primitive_topology_type);
        return nullptr;
    }
  }

  // Pixel shader.
  auto use_rov_depth_only_pixel_shader = [&]() {
    const XeDxilShaderBinary& bytecode =
        (as_interpreter ? native_rov_depth_pixel_shaders_ : native_guest_rov_depth_pixel_shaders_)[size_t(description.guest_msaa_samples)]
                                      [description.counting_depth_only ? 1 : 0];
    if (!bytecode.data) {
      XELOGE("Native ROV depth-only shader is unavailable");
      return false;
    }
    state_desc.PS.pShaderBytecode = bytecode.data;
    state_desc.PS.BytecodeLength = bytecode.size;
    return true;
  };
  if (as_placeholder) {
    // Hot-swap placeholder: the real pixel shader is not translated yet, so
    // rasterize without writing color. The flat-grey debug shader may stand in
    // on the host render target path to make the interim geometry visible.
    if (edram_rov_used) {
      // Color and depth both go through the pixel shader here, so the no-op
      // would draw nothing at all. Write depth like a depth-only draw.
      if (!use_rov_depth_only_pixel_shader()) return nullptr;
    } else if (as_interpreter &&
               cvars::async_shader_vs_interpreter_debug_color) {
      state_desc.PS.pShaderBytecode = shaders::placeholder_color_ps;
      state_desc.PS.BytecodeLength = sizeof(shaders::placeholder_color_ps);
    } else {
      state_desc.PS.pShaderBytecode = shaders::placeholder_ps;
      state_desc.PS.BytecodeLength = sizeof(shaders::placeholder_ps);
    }
  } else if (runtime_description.pixel_shader != nullptr) {
    if (runtime_description.native_pixel_dxil) {
      state_desc.PS.pShaderBytecode = runtime_description.native_pixel_dxil->data;
      state_desc.PS.BytecodeLength = runtime_description.native_pixel_dxil->size;
    } else {
      // Committed to the native path but the pixel DXIL is unavailable - fail
      // (the draw keeps the no-op placeholder).
      XELOGE("native_dxil: missing pixel DXIL for {:016X}",
             runtime_description.pixel_shader->shader().ucode_data_hash());
      return nullptr;
    }
  } else if (!edram_rov_used && description.counting_depth_only &&
             native_depth_only_pixel_shaders_[0][1].data) {
    unsigned depth_mode = 0;
    if (render_target_cache_.depth_float24_convert_in_pixel_shader() &&
        (description.depth_func != xenos::CompareFunction::kAlways ||
         description.depth_write) &&
        description.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
      depth_mode = render_target_cache_.depth_float24_round() ? 2 : 1;
    }
    const XeDxilShaderBinary& bytecode = native_depth_only_pixel_shaders_[depth_mode][1];
    state_desc.PS.pShaderBytecode = bytecode.data;
    state_desc.PS.BytecodeLength = bytecode.size;
  } else if (edram_rov_used) {
    // Guest depth-only draw on the ROV path.
    if (!use_rov_depth_only_pixel_shader()) return nullptr;
  } else {
    if (render_target_cache_.depth_float24_convert_in_pixel_shader() &&
        (description.depth_func != xenos::CompareFunction::kAlways ||
         description.depth_write) &&
        description.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
      if (render_target_cache_.depth_float24_round()) {
        state_desc.PS.pShaderBytecode = native_depth_only_pixel_shaders_[2][0].data;
        state_desc.PS.BytecodeLength = native_depth_only_pixel_shaders_[2][0].size;
      } else {
        state_desc.PS.pShaderBytecode = native_depth_only_pixel_shaders_[1][0].data;
        state_desc.PS.BytecodeLength = native_depth_only_pixel_shaders_[1][0].size;
      }
    } else if (!description.depth_write && !description.stencil_write_mask) {
      // Bind an empty PS to force rasterization.
      // D3D drops PS-less draws without depth/stencil writes, breaking
      // occlusion queries (4541096E, 5553083B).
      state_desc.PS.pShaderBytecode = native_depth_only_pixel_shaders_[0][0].data;
      state_desc.PS.BytecodeLength = native_depth_only_pixel_shaders_[0][0].size;
    }
  }

  // Native primitive expansion uses the guest inter-stage semantics and
  // existing system-constant root binding.
  if (runtime_description.native_geometry_dxil) {
    state_desc.GS.pShaderBytecode =
        runtime_description.native_geometry_dxil->data;
    state_desc.GS.BytecodeLength =
        runtime_description.native_geometry_dxil->size;
  }

  // Rasterizer state.
  state_desc.RasterizerState.FillMode = description.fill_mode_wireframe
                                            ? D3D12_FILL_MODE_WIREFRAME
                                            : D3D12_FILL_MODE_SOLID;
  switch (description.cull_mode) {
    case PipelineCullMode::kFront:
      state_desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
      break;
    case PipelineCullMode::kBack:
      state_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
      break;
    default:
      assert_true(description.cull_mode == PipelineCullMode::kNone ||
                  description.cull_mode ==
                      PipelineCullMode::kDisableRasterization);
      state_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      break;
  }
  state_desc.RasterizerState.FrontCounterClockwise =
      description.front_counter_clockwise ? true : false;
  state_desc.RasterizerState.DepthBias = description.depth_bias;
  state_desc.RasterizerState.DepthBiasClamp = 0.0f;
  // With non-square resolution scaling, make sure the worst-case impact is
  // reverted (slope only along the scaled axis), thus max. More bias is better
  // than less bias, because less bias means Z fighting with the background is
  // more likely. Native draws get the guest bias as is.
  state_desc.RasterizerState.SlopeScaledDepthBias =
      description.depth_bias_slope_scaled *
      (description.resolution_scale_native
           ? 1.0f
           : float(std::max(render_target_cache_.draw_resolution_scale_x(),
                            render_target_cache_.draw_resolution_scale_y())));
  state_desc.RasterizerState.DepthClipEnable =
      description.depth_clip ? true : false;
  uint32_t msaa_sample_count = uint32_t(1)
                               << uint32_t(description.host_msaa_samples);
  if (edram_rov_used) {
    // Only 1, 4, 8 and (not on all GPUs) 16 are allowed, using sample 0 as 0
    // and 3 as 1 for 2x instead (not exactly the same sample positions, but
    // still top-left and bottom-right - however, this can be adjusted with
    // programmable sample positions).
    assert_true(msaa_sample_count == 1 || msaa_sample_count == 4);
    if (msaa_sample_count != 1 && msaa_sample_count != 4) {
      return nullptr;
    }
    state_desc.RasterizerState.ForcedSampleCount =
        uint32_t(1) << uint32_t(description.host_msaa_samples);
  }

  // Sample mask and description.
  state_desc.SampleMask = UINT_MAX;
  // TODO(Triang3l): 4x MSAA fallback when 2x isn't supported without ROV.
  if (edram_rov_used) {
    state_desc.SampleDesc.Count = 1;
  } else {
    assert_true(msaa_sample_count <= 4);
    if (msaa_sample_count > 4) {
      return nullptr;
    }
    if (msaa_sample_count == 2 && !render_target_cache_.msaa_2x_supported()) {
      // Using sample 0 as 0 and 3 as 1 for 2x instead (not exactly the same
      // sample positions, but still top-left and bottom-right - however, this
      // can be adjusted with programmable sample positions).
      state_desc.SampleMask = 0b1001;
      state_desc.SampleDesc.Count = 4;
    } else {
      state_desc.SampleDesc.Count = msaa_sample_count;
    }
  }

  if (!edram_rov_used) {
    // Depth/stencil.
    if (description.depth_func != xenos::CompareFunction::kAlways ||
        description.depth_write) {
      state_desc.DepthStencilState.DepthEnable = true;
      state_desc.DepthStencilState.DepthWriteMask =
          description.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL
                                  : D3D12_DEPTH_WRITE_MASK_ZERO;
      // Comparison functions are the same in Direct3D 12 but plus one (minus
      // one, bit 0 for less, bit 1 for equal, bit 2 for greater).
      state_desc.DepthStencilState.DepthFunc =
          D3D12_COMPARISON_FUNC(uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
                                uint32_t(description.depth_func));
    }
    if (description.stencil_enable) {
      state_desc.DepthStencilState.StencilEnable = true;
      state_desc.DepthStencilState.StencilReadMask =
          description.stencil_read_mask;
      state_desc.DepthStencilState.StencilWriteMask =
          description.stencil_write_mask;
      // Stencil operations are the same in Direct3D 12 too but plus one.
      state_desc.DepthStencilState.FrontFace.StencilFailOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           uint32_t(description.stencil_front_fail_op));
      state_desc.DepthStencilState.FrontFace.StencilDepthFailOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           uint32_t(description.stencil_front_depth_fail_op));
      state_desc.DepthStencilState.FrontFace.StencilPassOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           uint32_t(description.stencil_front_pass_op));
      state_desc.DepthStencilState.FrontFace.StencilFunc =
          D3D12_COMPARISON_FUNC(uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
                                uint32_t(description.stencil_front_func));
      state_desc.DepthStencilState.BackFace.StencilFailOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           uint32_t(description.stencil_back_fail_op));
      state_desc.DepthStencilState.BackFace.StencilDepthFailOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           uint32_t(description.stencil_back_depth_fail_op));
      state_desc.DepthStencilState.BackFace.StencilPassOp =
          D3D12_STENCIL_OP(uint32_t(D3D12_STENCIL_OP_KEEP) +
                           uint32_t(description.stencil_back_pass_op));
      state_desc.DepthStencilState.BackFace.StencilFunc =
          D3D12_COMPARISON_FUNC(uint32_t(D3D12_COMPARISON_FUNC_NEVER) +
                                uint32_t(description.stencil_back_func));
    }
    if (state_desc.DepthStencilState.DepthEnable ||
        state_desc.DepthStencilState.StencilEnable) {
      state_desc.DSVFormat = D3D12RenderTargetCache::GetDepthDSVDXGIFormat(
          description.depth_format);
    }

    // Render targets and blending.
    state_desc.BlendState.IndependentBlendEnable = true;
    const bool alpha_blend_factor_supported =
        command_processor_.GetD3D12Provider().IsAlphaBlendFactorSupported();
    const D3D12_BLEND kBlendFactorMap[] = {
        D3D12_BLEND_ZERO,
        D3D12_BLEND_ONE,
        D3D12_BLEND_SRC_COLOR,
        D3D12_BLEND_INV_SRC_COLOR,
        D3D12_BLEND_SRC_ALPHA,
        D3D12_BLEND_INV_SRC_ALPHA,
        D3D12_BLEND_DEST_COLOR,
        D3D12_BLEND_INV_DEST_COLOR,
        D3D12_BLEND_DEST_ALPHA,
        D3D12_BLEND_INV_DEST_ALPHA,
        D3D12_BLEND_BLEND_FACTOR,
        D3D12_BLEND_INV_BLEND_FACTOR,
        D3D12_BLEND_SRC_ALPHA_SAT,
        alpha_blend_factor_supported ? D3D12_BLEND_ALPHA_FACTOR
                                     : D3D12_BLEND_BLEND_FACTOR,
        alpha_blend_factor_supported ? D3D12_BLEND_INV_ALPHA_FACTOR
                                     : D3D12_BLEND_INV_BLEND_FACTOR,
    };
    // 8 entries for safety since 3 bits from the guest are passed directly.
    static constexpr D3D12_BLEND_OP kBlendOpMap[] = {
        D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT,     D3D12_BLEND_OP_MIN,
        D3D12_BLEND_OP_MAX, D3D12_BLEND_OP_REV_SUBTRACT, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_ADD};
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      const PipelineRenderTarget& rt = description.render_targets[i];
      if (!rt.used) {
        // Null RTV descriptors can be used for slots with DXGI_FORMAT_UNKNOWN
        // in the pipeline state.
        state_desc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
        continue;
      }
      state_desc.NumRenderTargets = i + 1;
      state_desc.RTVFormats[i] =
          render_target_cache_.GetColorDrawDXGIFormat(rt.format);
      if (state_desc.RTVFormats[i] == DXGI_FORMAT_UNKNOWN) {
        assert_always();
        return nullptr;
      }
      D3D12_RENDER_TARGET_BLEND_DESC& blend_desc =
          state_desc.BlendState.RenderTarget[i];
      if (rt.src_blend != PipelineBlendFactor::kOne ||
          rt.dest_blend != PipelineBlendFactor::kZero ||
          rt.blend_op != xenos::BlendOp::kAdd ||
          rt.src_blend_alpha != PipelineBlendFactor::kOne ||
          rt.dest_blend_alpha != PipelineBlendFactor::kZero ||
          rt.blend_op_alpha != xenos::BlendOp::kAdd) {
        blend_desc.BlendEnable = true;
        blend_desc.BlendOp = kBlendOpMap[uint32_t(rt.blend_op)];
        if (blend_desc.BlendOp == D3D12_BLEND_OP_MIN ||
            blend_desc.BlendOp == D3D12_BLEND_OP_MAX) {
          blend_desc.SrcBlend = D3D12_BLEND_ONE;
          blend_desc.DestBlend = D3D12_BLEND_ONE;
          blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
          blend_desc.DestBlendAlpha = D3D12_BLEND_ONE;
        } else {
          blend_desc.SrcBlend = kBlendFactorMap[uint32_t(rt.src_blend)];
          blend_desc.DestBlend = kBlendFactorMap[uint32_t(rt.dest_blend)];
          blend_desc.SrcBlendAlpha =
              kBlendFactorMap[uint32_t(rt.src_blend_alpha)];
          blend_desc.DestBlendAlpha =
              kBlendFactorMap[uint32_t(rt.dest_blend_alpha)];
        }
        blend_desc.BlendOpAlpha = kBlendOpMap[uint32_t(rt.blend_op_alpha)];
      }
      // Not writing an output leaves the target undefined, not unchanged.
      blend_desc.RenderTargetWriteMask =
          state_desc.PS.pShaderBytecode == shaders::placeholder_ps
              ? 0
              : rt.write_mask;
    }
  }

  // Disable rasterization if needed (parameter combinations that make no
  // difference when rasterization is disabled have already been handled in
  // GetCurrentStateDescription) the way it's disabled in Direct3D by design
  // (disabling a pixel shader and depth / stencil).
  // TODO(Triang3l): When it happens to be that a combination of parameters
  // (no host pixel shader and depth / stencil without ROV) would disable
  // rasterization when it's still needed (for occlusion query sample counting),
  // ensure rasterization happens (by binding an empty pixel shader, or maybe
  // via ForcedSampleCount when not using 2x MSAA - its requirements for
  // OMSetRenderTargets need some investigation though).
  if (description.cull_mode == PipelineCullMode::kDisableRasterization) {
    state_desc.PS.pShaderBytecode = nullptr;
    state_desc.PS.BytecodeLength = 0;
    state_desc.DepthStencilState.DepthEnable = false;
    state_desc.DepthStencilState.StencilEnable = false;
  }

  // Create the D3D12 pipeline state object.
  ID3D12Device* device = command_processor_.GetD3D12Provider().GetDevice();
  ID3D12PipelineState* state;
  bool profile = cvars::shader_profiling;
  std::chrono::steady_clock::time_point pso_create_start;
  if (profile) {
    pso_create_start = std::chrono::steady_clock::now();
  }
  HRESULT hr =
      device->CreateGraphicsPipelineState(&state_desc, IID_PPV_ARGS(&state));
  if (SUCCEEDED(hr) && profile) {
    // Driver DXIL->ISA compile time.
    XELOGI(
        "shader_profiling: pipeline create{} VS {:016X} PS {:016X} {:.3f} ms",
        as_placeholder ? " (placeholder)" : "",
        runtime_description.vertex_shader
            ? runtime_description.vertex_shader->shader().ucode_data_hash()
            : 0,
        runtime_description.pixel_shader
            ? runtime_description.pixel_shader->shader().ucode_data_hash()
            : 0,
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pso_create_start)
            .count());
  }
  if (FAILED(hr)) {
    if (runtime_description.pixel_shader != nullptr) {
      XELOGE(
          "Failed to create graphics pipeline with VS {:016X}, PS {:016X}: "
          "HRESULT 0x{:08X}",
          runtime_description.vertex_shader->shader().ucode_data_hash(),
          runtime_description.pixel_shader->shader().ucode_data_hash(), hr);
    } else {
      XELOGE(
          "Failed to create graphics pipeline with VS {:016X}: HRESULT "
          "0x{:08X}",
          runtime_description.vertex_shader->shader().ucode_data_hash(), hr);
    }
    // Log D3D12 debug messages for all pipeline failures.
    command_processor_.GetD3D12Provider().LogD3D12DebugMessages();
    return nullptr;
  }
  std::wstring name;
  if (runtime_description.pixel_shader != nullptr) {
    name = fmt::format(
        L"VS {:016X}, PS {:016X}",
        runtime_description.vertex_shader->shader().ucode_data_hash(),
        runtime_description.pixel_shader->shader().ucode_data_hash());
  } else {
    name = fmt::format(
        L"VS {:016X}",
        runtime_description.vertex_shader->shader().ucode_data_hash());
  }
  state->SetName(name.c_str());
  return state;
}

ID3D12PipelineState* PipelineCache::CreateQueuedPipeline(
    Pipeline* pipeline) {
  // Build the deferred DXIL off the main thread.
  EnsurePipelineShadersTranslated(pipeline);
  return CreateD3D12Pipeline(pipeline->description);
}

void PipelineCache::StoreCreatedPipeline(Pipeline* pipeline,
                                         ID3D12PipelineState* state) {
  if (state != nullptr) {
    // Swap in the real pipeline. If a placeholder was in use, defer its
    // destruction until the GPU has passed the submissions that may reference
    // it. Without a placeholder the old value is null and this is a plain
    // store.
    ID3D12PipelineState* old_state =
        pipeline->state.exchange(state, std::memory_order_acq_rel);
    pipeline->is_placeholder.store(false, std::memory_order_release);
    if (old_state != nullptr) {
      std::lock_guard<std::mutex> lock(deferred_destroy_mutex_);
      deferred_destroy_pipelines_.emplace_back(
          old_state, command_processor_.GetCurrentSubmission());
    }
    pipeline->creation_pending.store(false, std::memory_order_release);
    return;
  }
  // Real creation failed. Keep any placeholder in use, but stop reporting it as
  // a placeholder so occlusion-query awaits do not block forever.
  pipeline->is_placeholder.store(false, std::memory_order_release);
  XELOGE("Pipeline creation failed (VS {:016X}, PS {:016X})",
         pipeline->description.vertex_shader
             ? pipeline->description.vertex_shader->shader().ucode_data_hash()
             : 0,
         pipeline->description.pixel_shader
             ? pipeline->description.pixel_shader->shader().ucode_data_hash()
             : 0);
  // After the log: this is what wants_rebuild() waits on, and the rebuild
  // overwrites the description the log just read.
  pipeline->creation_failed.store(true, std::memory_order_release);
  pipeline->creation_pending.store(false, std::memory_order_release);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

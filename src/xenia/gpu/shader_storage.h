/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_STORAGE_H_
#define XENIA_GPU_SHADER_STORAGE_H_

#include <cstdint>
#include <cstddef>
#include "libretro/core_cache_file.h"
#include "libretro/core_cache_writer.h"
#include "libretro/core_file.h"
#include <memory>
#include <file/file_path.h>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/base/xxhash.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

// Shader storage file format (.xsh) - shared between D3D12 and Vulkan backends.
// Stores guest shader microcode (Xbox 360 bytecode) for persistent caching.

// File magic: 'XESH'
constexpr uint32_t kShaderStorageMagic = 0x48534558;

// Header for each stored shader entry.
XEPACKEDSTRUCT(ShaderStoredHeader, {
  uint64_t ucode_data_hash;

  uint32_t ucode_dword_count : 31;
  xenos::ShaderType type : 1;

  // Increment when the format changes in an incompatible way.
  static constexpr uint32_t kVersion = 0x20201219;
});

// Pipeline storage file format (.xpso) - API-specific but with shared header.
// File magic: 'XEPS'
constexpr uint32_t kPipelineStorageMagic = 0x53504558;

// Storage directory structure:
//   <cache_root>/shaders/
//     shareable/           - Files shared between different machines/drivers
//       {TITLE_ID}.xsh     - Shader microcode (shared between D3D12/Vulkan)
//       {TITLE_ID}.*.xpso  - Pipeline descriptions (API-specific)
//     local/               - Files specific to this machine/driver
//       {TITLE_ID}.*.bin   - Driver-specific pipeline cache

inline std::filesystem::path GetShaderStorageRoot(
    const std::filesystem::path& cache_root) {
  return cache_root / "shaders";
}

inline std::filesystem::path GetShaderStorageShareableRoot(
    const std::filesystem::path& cache_root) {
  return GetShaderStorageRoot(cache_root) / "shareable";
}

inline std::filesystem::path GetShaderStorageLocalRoot(
    const std::filesystem::path& cache_root) {
  return GetShaderStorageRoot(cache_root) / "local";
}

inline std::filesystem::path GetShaderStorageFilePath(
    const std::filesystem::path& cache_root, uint32_t title_id) {
  return GetShaderStorageShareableRoot(cache_root) /
         fmt::format("{:08X}.xsh", title_id);
}

// Ensures shader storage directories exist. Returns true on success.
inline bool EnsureShaderStorageDirectoriesExist(
    const std::filesystem::path& cache_root) {
  const auto shareable_root = xe::path_to_utf8(GetShaderStorageShareableRoot(cache_root));
  if (!path_mkdir(shareable_root.c_str())) {
    XELOGE("Failed to create shader storage directory: {}", shareable_root);
    return false;
  }
  return true;
}

// File header for shader storage (.xsh).
XEPACKEDSTRUCT(ShaderStorageFileHeader, {
  uint32_t magic;
  uint32_t version_swapped;
});

// Validates the shader storage file header. Returns true if valid.
inline bool ValidateShaderStorageHeader(RFILE* file,
                                        ShaderStorageFileHeader& header_out) {
  if (!xe_cache_file_read(&header_out, sizeof(header_out), 1, file)) {
    return false;
  }
  return header_out.magic == kShaderStorageMagic &&
         xe::byte_swap(header_out.version_swapped) ==
             ShaderStoredHeader::kVersion;
}

// Writes a new shader storage file header.
inline bool WriteShaderStorageHeader(RFILE* file) {
  ShaderStorageFileHeader header;
  header.magic = kShaderStorageMagic;
  header.version_swapped = xe::byte_swap(ShaderStoredHeader::kVersion);
  return xe_cache_file_write(&header, sizeof(header), 1, file) == 1;
}

// Reads shader entries from storage file and calls the callback for each.
// Returns the number of valid bytes read (for truncation on corruption).
// The callback signature is: bool(xenos::ShaderType type,
//                                  const uint32_t* ucode_dwords,
//                                  uint32_t ucode_dword_count,
//                                  uint64_t ucode_data_hash)
// Callback should return true to continue reading, false to stop.
template <typename Callback>
inline uint64_t ReadShaderEntries(RFILE* file, Callback&& callback) {
  uint64_t valid_bytes = sizeof(ShaderStorageFileHeader);

  ShaderStoredHeader shader_header;
  std::vector<uint32_t> ucode_dwords;

  while (true) {
    if (!xe_cache_file_read(&shader_header, sizeof(shader_header), 1, file)) {
      break;
    }
    const uint64_t requested_bytes =
        uint64_t(shader_header.ucode_dword_count) * sizeof(uint32_t);
    if (requested_bytes > SIZE_MAX || !xe_cache_file_has_bytes(file, requested_bytes)) {
      break;
    }
    const size_t ucode_byte_count = size_t(requested_bytes);
    ucode_dwords.resize(shader_header.ucode_dword_count);
    if (shader_header.ucode_dword_count &&
        !xe_cache_file_read(ucode_dwords.data(), ucode_byte_count, 1, file)) {
      break;
    }
    uint64_t ucode_data_hash =
        XXH3_64bits(ucode_dwords.data(), ucode_byte_count);
    if (shader_header.ucode_data_hash != ucode_data_hash) {
      // Validation failed - corrupted entry.
      break;
    }
    valid_bytes += sizeof(shader_header) + ucode_byte_count;

    if (!callback(shader_header.type, ucode_dwords.data(),
                  shader_header.ucode_dword_count, ucode_data_hash)) {
      break;
    }
  }

  return valid_bytes;
}

// File header for pipeline storage (.xpso).
XEPACKEDSTRUCT(PipelineStorageFileHeader, {
  uint32_t magic;
  uint32_t magic_api;
  uint32_t version_swapped;
});

using PipelineStorageRecords = std::unique_ptr<xe_file_records, decltype(&xe_file_records_close)>;

// Template class for shader and pipeline storage management.
// TPipelineStoredDescription is the API-specific pipeline description type.
template <typename TPipelineStoredDescription>
class ShaderStorageWriter {
 public:
  // Configuration for pipeline storage files.
  struct PipelineStorageConfig {
    std::string file_suffix;  // e.g., ".fsi.vk.xpso" or ".rov.d3d12.xpso"
    uint32_t api_magic;       // e.g., 'VKPS' or 'DXRO'
    uint32_t version;         // Pipeline description version
  };

  // Callback for loading a shader from ucode data.
  // Return true to continue reading, false to stop.
  using ShaderLoadCallback =
      std::function<bool(xenos::ShaderType type, const uint32_t* ucode_dwords,
                         uint32_t ucode_dword_count, uint64_t ucode_data_hash)>;

  // Callback for translating shaders. Called with the set of
  // (ucode_hash, modification) pairs that need translation.
  // Implementation should handle parallel translation internally.
  using TranslateCallback = std::function<void(
      const std::set<std::pair<uint64_t, uint64_t>>& translations_needed)>;

  ShaderStorageWriter() = default;
  ~ShaderStorageWriter() { ShutdownShaderStorage(); }

  // Non-copyable.
  ShaderStorageWriter(const ShaderStorageWriter&) = delete;
  ShaderStorageWriter& operator=(const ShaderStorageWriter&) = delete;

  // Opens files, loads shaders, triggers translation, starts write thread.
  // Caller must call ShutdownShaderStorage() first if re-initializing.
  bool InitializeShaderStorage(
      const std::filesystem::path& cache_root, uint32_t title_id,
      const PipelineStorageConfig& pipeline_config,
      ShaderLoadCallback load_shader, TranslateCallback translate_shaders,
      PipelineStorageRecords& pipeline_descriptions_out) {
    pipeline_descriptions_out.reset();
    std::set<std::pair<uint64_t, uint64_t>> translations_needed;
    struct RecordContext {
      TPipelineStoredDescription desc;
      std::set<std::pair<uint64_t, uint64_t>>* translations;
    } record_context;
    record_context.translations = &translations_needed;
    cache_root_ = cache_root;
    title_id_ = title_id;

    if (!EnsureShaderStorageDirectoriesExist(cache_root)) {
      return false;
    }

    auto shader_storage_shareable_root =
        GetShaderStorageShareableRoot(cache_root);
    ++storage_index_;

    // Open pipeline storage file.
    auto pipeline_storage_file_path =
        shader_storage_shareable_root /
        fmt::format("{:08X}{}", title_id, pipeline_config.file_suffix);
    pipeline_storage_file_ =
        xe_cache_file_open(xe::path_to_utf8(pipeline_storage_file_path).c_str());
    if (!pipeline_storage_file_) {
      XELOGE(
          "Failed to open the pipeline storage file for writing, persistent "
          "shader storage will be disabled: {}",
          xe::path_to_utf8(pipeline_storage_file_path));
      return false;
    }
    // Read the header explicitly; repair below selects the append position.
    if (filestream_seek(pipeline_storage_file_, 0, RETRO_VFS_SEEK_POSITION_START) < 0) {
      return CloseStorageFilesAfterInitializationFailure();
    }

    // Read pipeline descriptions.
    const uint32_t pipeline_storage_version_swapped =
        xe::byte_swap(pipeline_config.version);
    PipelineStorageFileHeader pipeline_header;
    size_t pipeline_description_count = 0;
    if (xe_cache_file_read(&pipeline_header, sizeof(pipeline_header), 1,
              pipeline_storage_file_) &&
        pipeline_header.magic == kPipelineStorageMagic &&
        pipeline_header.magic_api == pipeline_config.api_magic &&
        pipeline_header.version_swapped == pipeline_storage_version_swapped) {
      // Validate raw bytes through a mapping or bounded reusable VFS window.
      // Close this view before repairing the file (required by Windows mappings).
      PipelineStorageRecords records(xe_file_records_open(
          xe::path_to_utf8(pipeline_storage_file_path).c_str(), sizeof(pipeline_header),
          sizeof(TPipelineStoredDescription)), xe_file_records_close);
      if (!records) return CloseStorageFilesAfterInitializationFailure();
      if (!xe_cache_records_validate(records.get(), sizeof(TPipelineStoredDescription),
          offsetof(TPipelineStoredDescription, description_hash),
          offsetof(TPipelineStoredDescription, description),
          sizeof(TPipelineStoredDescription::description),
          [](void* opaque, const uint8_t* bytes, size_t size) -> int {
            RecordContext* context = static_cast<RecordContext*>(opaque);
            std::memcpy(static_cast<void*>(&context->desc), bytes, size);
            const auto& desc = context->desc.description;
            context->translations->emplace(desc.vertex_shader_hash, desc.vertex_shader_modification);
            if (desc.pixel_shader_hash)
              context->translations->emplace(desc.pixel_shader_hash, desc.pixel_shader_modification);
            return 1;
          }, &record_context, &pipeline_description_count)) {
        return CloseStorageFilesAfterInitializationFailure();
      }
      records.reset();
      // Truncate to last valid description.
      if (!xe_cache_file_repair(
          pipeline_storage_file_,
          uint64_t(sizeof(pipeline_header) +
                   sizeof(TPipelineStoredDescription) *
                       pipeline_description_count))) {
        return CloseStorageFilesAfterInitializationFailure();
      }
    } else {
      // Write new header.
      if (!xe_cache_file_repair(pipeline_storage_file_, 0)) {
        return CloseStorageFilesAfterInitializationFailure();
      }
      pipeline_header.magic = kPipelineStorageMagic;
      pipeline_header.magic_api = pipeline_config.api_magic;
      pipeline_header.version_swapped = pipeline_storage_version_swapped;
      if (xe_cache_file_write(&pipeline_header, sizeof(pipeline_header), 1,
                 pipeline_storage_file_) != 1) {
        XELOGE("Failed to write pipeline storage header");
        filestream_close(pipeline_storage_file_);
        pipeline_storage_file_ = nullptr;
        return false;
      }
    }

    // Open shader storage file.
    auto shader_storage_file_path =
        GetShaderStorageFilePath(cache_root, title_id);
    shader_storage_file_ =
        xe_cache_file_open(xe::path_to_utf8(shader_storage_file_path).c_str());
    if (!shader_storage_file_) {
      XELOGE(
          "Failed to open the guest shader storage file for writing, "
          "persistent shader storage will be disabled: {}",
          xe::path_to_utf8(shader_storage_file_path));
      filestream_close(pipeline_storage_file_);
      pipeline_storage_file_ = nullptr;
      return false;
    }
    if (filestream_seek(shader_storage_file_, 0, RETRO_VFS_SEEK_POSITION_START) < 0) {
      return CloseStorageFilesAfterInitializationFailure();
    }

    // Load shaders from storage.
    size_t shaders_loaded = 0;
    ShaderStorageFileHeader shader_header;
    if (ValidateShaderStorageHeader(shader_storage_file_, shader_header)) {
      uint64_t shader_storage_valid_bytes = ReadShaderEntries(
          shader_storage_file_,
          [&](xenos::ShaderType type, const uint32_t* ucode_dwords,
              uint32_t ucode_dword_count, uint64_t ucode_data_hash) {
            if (load_shader(type, ucode_dwords, ucode_dword_count,
                            ucode_data_hash)) {
              ++shaders_loaded;
              return true;
            }
            return false;
          });
      if (!xe_cache_file_repair(shader_storage_file_,
                                        shader_storage_valid_bytes)) {
        return CloseStorageFilesAfterInitializationFailure();
      }
    } else {
      // Write new header.
      if (!xe_cache_file_repair(shader_storage_file_, 0)) {
        return CloseStorageFilesAfterInitializationFailure();
      }
      if (!WriteShaderStorageHeader(shader_storage_file_)) {
        XELOGE("Failed to write shader storage header");
        filestream_close(shader_storage_file_);
        shader_storage_file_ = nullptr;
        filestream_close(pipeline_storage_file_);
        pipeline_storage_file_ = nullptr;
        return false;
      }
    }
    XELOGI("Loaded {} shaders from storage", shaders_loaded);

    // Reopen the validated prefix only after repair/header output is visible.
    if (filestream_flush(pipeline_storage_file_) != 0)
      return CloseStorageFilesAfterInitializationFailure();
    PipelineStorageRecords records(xe_file_records_open(
        xe::path_to_utf8(pipeline_storage_file_path).c_str(), sizeof(pipeline_header),
        sizeof(TPipelineStoredDescription)), xe_file_records_close);
    if (!records || xe_file_records_count(records.get()) != pipeline_description_count)
      return CloseStorageFilesAfterInitializationFailure();

    // Translation keys were collected during validation, avoiding another file pass.
    XELOGI("Loaded {} pipeline descriptions, {} shader translations needed",
           pipeline_description_count, translations_needed.size());

    // Translate shaders (callback handles parallel translation).
    if (!translations_needed.empty() && translate_shaders) {
      translate_shaders(translations_needed);
    }

    // ConfigurePipeline / EndSubmission are the sole GPU-thread producer.
    // Keep file writes off that thread; bound all pending record storage.
    storage_writer_ = xe_cache_writer_new(sizeof(TPipelineStoredDescription),
        16384, &WriteRecord, this);
    if (!storage_writer_) return CloseStorageFilesAfterInitializationFailure();
    pipeline_descriptions_out = std::move(records);

    return true;
  }

  // Shutdown storage: stops write thread, closes files.
  void ShutdownShaderStorage() {
    // The command processor has stopped producing. Drain before shader owners
    // or storage files are released; no frontend/GPU progress is needed here.
    xe_cache_writer_free(storage_writer_);
    storage_writer_ = nullptr;

    if (pipeline_storage_file_) {
      filestream_close(pipeline_storage_file_);
      pipeline_storage_file_ = nullptr;
    }
    if (shader_storage_file_) {
      filestream_close(shader_storage_file_);
      shader_storage_file_ = nullptr;
    }

    cache_root_.clear();
    title_id_ = 0;
  }

  uint32_t storage_index() const { return storage_index_; }
  RFILE* shader_storage_file() const { return shader_storage_file_; }
  RFILE* pipeline_storage_file() const { return pipeline_storage_file_; }
  bool is_active() const { return shader_storage_file_ != nullptr; }
  const std::filesystem::path& cache_root() const { return cache_root_; }
  uint32_t title_id() const { return title_id_; }

  void QueueShaderWrite(const Shader* shader) {
    xe_cache_writer_submit(storage_writer_, 1, &shader, sizeof(shader));
  }

  void QueuePipelineWrite(uint64_t description_hash, const void* description) {
    // The persistent format is a packed hash followed by descriptor bytes.
    static_assert(offsetof(TPipelineStoredDescription, description) ==
                  sizeof(uint64_t));
    static_assert(sizeof(TPipelineStoredDescription) == sizeof(uint64_t) +
                  sizeof(TPipelineStoredDescription::description));
    xe_cache_writer_submit_parts(storage_writer_, 2,
        &description_hash, sizeof(description_hash), description,
        sizeof(TPipelineStoredDescription::description));
  }

  void RequestFlush(bool flush_shaders, bool flush_pipelines) {
    if (flush_shaders || flush_pipelines)
      xe_cache_writer_submit(storage_writer_,
          4 | (flush_shaders ? 1 : 0) | (flush_pipelines ? 2 : 0), nullptr, 0);
  }

 private:
  bool CloseStorageFilesAfterInitializationFailure() {
    if (shader_storage_file_) filestream_close(shader_storage_file_);
    if (pipeline_storage_file_) filestream_close(pipeline_storage_file_);
    shader_storage_file_ = pipeline_storage_file_ = nullptr;
    return false;
  }

  static void WriteRecord(void* opaque, unsigned kind,
                          const void* data, size_t size) {
    auto* self = static_cast<ShaderStorageWriter*>(opaque);
    if (kind & 4) {
      if ((kind & 1) && filestream_flush(self->shader_storage_file_) != 0)
        XELOGE("Failed to flush shader storage");
      if ((kind & 2) && filestream_flush(self->pipeline_storage_file_) != 0)
        XELOGE("Failed to flush pipeline storage");
    } else if (kind == 1 && size == sizeof(const Shader*)) {
      const Shader* shader;
      std::memcpy(&shader, data, sizeof(shader));
      ShaderStoredHeader header;
      std::memset(&header, 0, sizeof(header));
      header.ucode_data_hash = shader->ucode_data_hash();
      header.ucode_dword_count = shader->ucode_dword_count();
      header.type = shader->type();
      if (xe_cache_file_write(&header, sizeof(header), 1,
                              self->shader_storage_file_) != 1) {
        XELOGE("Failed to write shader header to storage");
      } else if (header.ucode_dword_count &&
          !xe_cache_file_write_be32(self->shader_storage_file_,
              shader->ucode_dwords(), header.ucode_dword_count)) {
        XELOGE("Failed to write shader ucode to storage");
      }
    } else if (kind == 2 && size == sizeof(TPipelineStoredDescription)) {
      // Write the owned queue slot directly; no temporary C++ descriptor copy.
      if (xe_cache_file_write(data, size, 1, self->pipeline_storage_file_) != 1)
        XELOGE("Failed to write pipeline description to storage");
    }
  }

  std::filesystem::path cache_root_;
  uint32_t title_id_ = 0;
  uint32_t storage_index_ = 0;

  RFILE* shader_storage_file_ = nullptr;
  RFILE* pipeline_storage_file_ = nullptr;

  xe_cache_writer* storage_writer_ = nullptr;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHADER_STORAGE_H_

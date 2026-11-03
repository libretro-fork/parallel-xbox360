/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/zar_metadata.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>

#include "libretro/core_zarchive.h"
#include "xenia/base/filesystem.h"

namespace xe {
namespace vfs {

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// Recursively search for default.xex in the archive.
uint32_t FindDefaultXex(xe_zar* reader,
                                  const std::string& dir_path, int max_depth) {
  if (max_depth <= 0) {
    return XE_ZAR_INVALID;
  }

  uint32_t dir_handle = xe_zar_lookup(reader, dir_path.data(), dir_path.size(), 0, 1);
  if (dir_handle == XE_ZAR_INVALID || !(xe_zar_type(reader, dir_handle) == 0)) {
    return XE_ZAR_INVALID;
  }

  uint32_t entry_count = xe_zar_count(reader, dir_handle);
  for (uint32_t i = 0; i < entry_count; i++) {
    xe_zar_entry entry;
    if (!xe_zar_entry_get(reader, dir_handle, i, &entry)) {
      continue;
    }

    if (entry.is_file && EqualsIgnoreCase(std::string_view(entry.name, entry.name_size), "default.xex")) {
      std::string file_path = dir_path;
      if (!file_path.empty() && file_path.back() != '/') {
        file_path += '/';
      }
      file_path += std::string(entry.name, entry.name_size);
      return xe_zar_lookup(reader, file_path.data(), file_path.size(), 1, 0);
    }

    if (!entry.is_file) {
      std::string subdir_path = dir_path;
      if (!subdir_path.empty() && subdir_path.back() != '/') {
        subdir_path += '/';
      }
      subdir_path += std::string(entry.name, entry.name_size);

      uint32_t result =
          FindDefaultXex(reader, subdir_path, max_depth - 1);
      if (result != XE_ZAR_INVALID) {
        return result;
      }
    }
  }

  return XE_ZAR_INVALID;
}

}  // namespace

std::optional<XexMetadata> ExtractZarMetadata(
    const std::filesystem::path& path) {
  std::unique_ptr<xe_zar, decltype(&xe_zar_close)> reader(xe_zar_open(xe::path_to_utf8(path).c_str()), &xe_zar_close);
  if (!reader) {
    return std::nullopt;
  }

  uint32_t handle = FindDefaultXex(reader.get(), "/", 2);
  if (handle == XE_ZAR_INVALID) {
    handle = FindDefaultXex(reader.get(), "", 2);
  }
  if (handle == XE_ZAR_INVALID || !(xe_zar_type(reader.get(), handle) == 1)) {
    return std::nullopt;
  }

  struct ArchiveSource { xe_zar* reader; uint32_t node; } context = {reader.get(), handle};
  xe_xex_source source = {};
  source.context = &context;
  source.size = xe_zar_size(reader.get(), handle);
  source.read = [](void* opaque, uint64_t offset, void* out, size_t size) -> int {
    auto* context = static_cast<ArchiveSource*>(opaque);
    return xe_zar_read(context->reader, context->node, offset, size, out) == size;
  };
  source.view = [](void* opaque, uint64_t offset, size_t size) {
    auto* context = static_cast<ArchiveSource*>(opaque);
    return xe_zar_view(context->reader, context->node, offset, size);
  };
  return ExtractXexMetadata(source);
}

}  // namespace vfs
}  // namespace xe

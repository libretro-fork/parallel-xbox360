/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/xex_metadata.h"

#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

#include "third_party/fmt/include/fmt/format.h"
#include "libretro/core_file.h"
#include "xenia/base/filesystem.h"

namespace xe {
namespace vfs {

std::string XexVersion::ToString() const {
  return fmt::format("{}.{}.{}.{}", major, minor, build, qfe);
}

uint32_t XexVersion::value() const {
  // xex2_version bit layout: major:4, minor:4, build:16, qfe:8
  return (uint32_t(major & 0xF) << 28) | (uint32_t(minor & 0xF) << 24) |
         (uint32_t(build) << 8) | uint32_t(qfe);
}

XexVersion XexVersion::FromValue(uint32_t value) {
  XexVersion v;
  v.major = (value >> 28) & 0xF;
  v.minor = (value >> 24) & 0xF;
  v.build = (value >> 8) & 0xFFFF;
  v.qfe = value & 0xFF;
  return v;
}

std::optional<XexVersion> XexVersion::FromString(std::string_view text) {
  uint32_t parts[4] = {};
  size_t at = 0;
  for (size_t i = 0; i < 4; ++i) {
    const size_t start = at;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9' &&
           at - start < 5) {
      parts[i] = parts[i] * 10 + uint32_t(text[at] - '0');
      ++at;
    }
    // No digits, or a leading zero, which would spell one number two ways.
    if (at == start || (text[start] == '0' && at - start > 1)) {
      return std::nullopt;
    }
    if (i < 3) {
      if (at >= text.size() || text[at] != '.') {
        return std::nullopt;
      }
      ++at;
    }
  }
  if (at != text.size() || parts[0] > 0xF || parts[1] > 0xF ||
      parts[2] > 0xFFFF || parts[3] > 0xFF) {
    return std::nullopt;
  }
  XexVersion v;
  v.major = uint8_t(parts[0]);
  v.minor = uint8_t(parts[1]);
  v.build = uint16_t(parts[2]);
  v.qfe = uint8_t(parts[3]);
  return v;
}

toml::table XexMetadata::ToToml() const {
  toml::table root;

  toml::table module_table;
  module_table.insert("name", module_name);
  switch (format) {
    case XexFormat::kXex1:
      module_table.insert("format", "XEX1");
      break;
    case XexFormat::kXex2:
      module_table.insert("format", "XEX2");
      break;
    default:
      module_table.insert("format", "unknown");
      break;
  }
  root.insert("module", std::move(module_table));

  toml::table exec_table;
  exec_table.insert("title_id", fmt::format("{:08X}", title_id));
  exec_table.insert("media_id", fmt::format("{:08X}", media_id));
  exec_table.insert("savegame_id", fmt::format("{:08X}", savegame_id));
  exec_table.insert("version", version.ToString());
  exec_table.insert("base_version", base_version.ToString());
  exec_table.insert("disc_number", static_cast<int64_t>(disc_number));
  exec_table.insert("disc_count", static_cast<int64_t>(disc_count));
  root.insert("execution_info", std::move(exec_table));

  if (ratings.present) {
    toml::table ratings_table;
    if (ratings.esrb != 0xFF) {
      ratings_table.insert("esrb", static_cast<int64_t>(ratings.esrb));
    }
    if (ratings.pegi != 0xFF) {
      ratings_table.insert("pegi", static_cast<int64_t>(ratings.pegi));
    }
    if (ratings.cero != 0xFF) {
      ratings_table.insert("cero", static_cast<int64_t>(ratings.cero));
    }
    if (ratings.usk != 0xFF) {
      ratings_table.insert("usk", static_cast<int64_t>(ratings.usk));
    }
    if (ratings.oflc_au != 0xFF) {
      ratings_table.insert("oflc_au", static_cast<int64_t>(ratings.oflc_au));
    }
    if (ratings.oflc_nz != 0xFF) {
      ratings_table.insert("oflc_nz", static_cast<int64_t>(ratings.oflc_nz));
    }
    if (!ratings_table.empty()) {
      root.insert("ratings", std::move(ratings_table));
    }
  }

  return root;
}

std::optional<XexMetadata> ExtractXexMetadata(const xe_xex_source& source) {
  xe_xex_metadata parsed;
  if (!xe_xex_parse(&source, &parsed)) return std::nullopt;
  try {
    XexMetadata m;
    m.format = parsed.format == 1 ? XexFormat::kXex1 : XexFormat::kXex2;
    m.title_id = parsed.title_id;
    m.media_id = parsed.media_id;
    m.savegame_id = parsed.savegame_id;
    m.version = XexVersion::FromValue(parsed.version);
    m.base_version = XexVersion::FromValue(parsed.base_version);
    m.disc_number = parsed.disc_number;
    m.disc_count = parsed.disc_count;
    m.ratings.present = parsed.ratings_present != 0;
    m.ratings.esrb = parsed.esrb;
    m.ratings.pegi = parsed.pegi;
    m.ratings.cero = parsed.cero;
    m.ratings.usk = parsed.usk;
    m.ratings.oflc_au = parsed.oflc_au;
    m.ratings.oflc_nz = parsed.oflc_nz;
    if (parsed.name_size) {
      const uint8_t* name = source.view ? source.view(source.context,
          parsed.name_offset, parsed.name_size) : nullptr;
      if (name) {
        m.module_name.assign(reinterpret_cast<const char*>(name), parsed.name_size);
      } else {
        m.module_name.resize(parsed.name_size);
        if (!source.read || !source.read(source.context, parsed.name_offset,
            m.module_name.data(), parsed.name_size)) return std::nullopt;
      }
    }
    return m;
  } catch (const std::bad_alloc&) {
    return std::nullopt;
  } catch (const std::length_error&) {
    return std::nullopt;
  }
}

std::optional<XexMetadata> ExtractXexMetadata(const uint8_t* data, size_t size) {
  if (!data) return std::nullopt;
  xe_xex_source source = {};
  source.context = const_cast<uint8_t*>(data);
  source.size = size;
  source.view = [](void* context, uint64_t offset, size_t) -> const uint8_t* {
    return static_cast<const uint8_t*>(context) + size_t(offset);
  };
  return ExtractXexMetadata(source);
}

std::optional<XexMetadata> ExtractXexMetadata(const std::filesystem::path& path) {
  std::unique_ptr<xe_file, decltype(&xe_file_close)> file(
      xe_file_open(xe::path_to_utf8(path).c_str()), &xe_file_close);
  if (!file) return std::nullopt;
  xe_xex_source source = {};
  source.context = file.get();
  source.size = xe_file_size(file.get());
  source.read = [](void* context, uint64_t offset, void* out, size_t size) {
    return xe_file_read(static_cast<xe_file*>(context), offset, out, size);
  };
  source.view = [](void* context, uint64_t offset, size_t size) {
    return xe_file_view(static_cast<xe_file*>(context), offset, size);
  };
  return ExtractXexMetadata(source);
}

}  // namespace vfs
}  // namespace xe

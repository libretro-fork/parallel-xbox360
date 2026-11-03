/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_zarchive_device.h"

#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/vfs/devices/disc_zarchive_entry.h"

#include "libretro/core_zarchive.h"
#include "xenia/base/filesystem.h"

namespace xe {
namespace vfs {

DiscZarchiveDevice::DiscZarchiveDevice(const std::string_view mount_path,
                                       const std::filesystem::path& host_path)
    : Device(mount_path), name_("GDFX"), host_path_(host_path) {}

DiscZarchiveDevice::~DiscZarchiveDevice() {};

bool DiscZarchiveDevice::Initialize() {
  reader_.reset(xe_zar_open(xe::path_to_utf8(host_path_).c_str()));

  if (!reader_) {
    XELOGE("Disc ZArchive could not be opened");
    return false;
  }

  constexpr std::string_view root_path = "/";
  const uint32_t handle = xe_zar_lookup(reader_.get(), root_path.data(), root_path.size(), 1, 1);
  XELOGI("DiscZarchiveDevice::Initialize: root LookUp('{}') -> handle={}",
         root_path, static_cast<uint32_t>(handle));
  auto root_entry = new DiscZarchiveEntry(this, nullptr, root_path);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry->handle_ = static_cast<uint32_t>(handle);
  root_entry->name_ = root_path;
  root_entry->absolute_path_ = root_path;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  bool result = ReadAllEntries("", root_entry, nullptr);
  XELOGI(
      "DiscZarchiveDevice::Initialize: ReadAllEntries returned {}, "
      "root has {} children",
      result, root_entry->children().size());
  return result;
}

void DiscZarchiveDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* DiscZarchiveDevice::ResolvePath(const std::string_view path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo
  XELOGFS("DiscZarchiveDevice::ResolvePath({})", path);
  XELOGI("DiscZarchiveDevice::ResolvePath: path='{}'", path);

  if (!reader_) {
    XELOGI("DiscZarchiveDevice::ResolvePath: reader_ is null");
    return nullptr;
  }

  const uint32_t handle = xe_zar_lookup(reader_.get(), path.data(), path.size(), 1, 1);
  XELOGI(
      "DiscZarchiveDevice::ResolvePath: LookUp('{}') -> handle={} (invalid={})",
      path, static_cast<uint32_t>(handle), handle == XE_ZAR_INVALID);
  if (handle == XE_ZAR_INVALID) {
    return nullptr;
  }

  Entry* result = root_entry_->ResolvePath(path);
  XELOGI("DiscZarchiveDevice::ResolvePath: entry tree walk -> {}",
         result ? result->absolute_path() : "(null)");
  return result;
}

bool DiscZarchiveDevice::ReadAllEntries(const std::string& path,
                                        DiscZarchiveEntry* node,
                                        DiscZarchiveEntry* parent) {
  const uint32_t handle = node->handle_;

  if (handle == XE_ZAR_INVALID) {
    return false;
  }

  if ((xe_zar_type(reader_.get(), handle) == 1)) {
    auto entry = new DiscZarchiveEntry(this, parent, path);
    entry->attributes_ = kFileAttributeReadOnly;
    entry->handle_ = static_cast<uint32_t>(handle);
    entry->parent_ = parent;
    entry->children_.push_back(std::unique_ptr<Entry>(entry));
    return true;
  }

  if ((xe_zar_type(reader_.get(), handle) == 0)) {
    const uint32_t count = xe_zar_count(reader_.get(), handle);

    for (uint32_t i = 0; i < count; i++) {
      xe_zar_entry dirEntry;
      if (!xe_zar_entry_get(reader_.get(), handle, i, &dirEntry)) {
        XELOGE("Invalid ZArchive directory! Skipping loading");
        return false;
      }

      const std::string full_path = path + std::string(dirEntry.name, dirEntry.name_size);
      const uint32_t fileHandle = xe_zar_lookup(reader_.get(), full_path.data(), full_path.size(), 1, 1);
      XELOGI(
          "DiscZarchiveDevice::ReadAllEntries: entry='{}' type={} "
          "handle={} size={}",
          full_path,
          !dirEntry.is_file ? "dir" : (dirEntry.is_file ? "file" : "unknown"),
          static_cast<uint32_t>(fileHandle), dirEntry.size);
      if (fileHandle == XE_ZAR_INVALID) {
        XELOGE("DiscZarchiveDevice::ReadAllEntries: LookUp failed for '{}'",
               full_path);
        return false;
      }

      auto entry = new DiscZarchiveEntry(this, parent, full_path);
      entry->handle_ = static_cast<uint32_t>(fileHandle);
      entry->data_offset_ = 0;

      // Set to January 1, 1970 (UTC) in 100-nanosecond intervals
      entry->create_timestamp_ = 10000 * 11644473600000LL;
      entry->access_timestamp_ = 10000 * 11644473600000LL;
      entry->write_timestamp_ = 10000 * 11644473600000LL;
      entry->parent_ = node;

      if (!dirEntry.is_file) {
        entry->data_size_ = 0;
        entry->size_ = dirEntry.size;
        entry->attributes_ = kFileAttributeDirectory | kFileAttributeReadOnly;
        node->children_.push_back(std::unique_ptr<Entry>(entry));
        if (!ReadAllEntries(full_path + "\\", entry, node)) {
          return false;
        }
      } else if (dirEntry.is_file) {
        entry->data_size_ = entry->size_ = xe_zar_size(reader_.get(), fileHandle);
        entry->attributes_ = kFileAttributeReadOnly;
        entry->allocation_size_ =
            xe::round_up(entry->size_, bytes_per_sector());
        node->children_.push_back(std::unique_ptr<Entry>(entry));
      }
    }
    return true;
  }

  return false;
}

}  // namespace vfs
}  // namespace xe

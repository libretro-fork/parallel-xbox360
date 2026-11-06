/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/disc_zarchive_entry.h"

#include "xenia/base/math.h"
#include "xenia/vfs/devices/disc_zarchive_file.h"
#include "xenia/vfs/devices/disc_zarchive_device.h"

#include "libretro/core_zarchive.h"

namespace xe {
namespace vfs {

DiscZarchiveEntry::DiscZarchiveEntry(Device* device, Entry* parent,
                                     const std::string_view path)
    : Entry(device, parent, path),
      data_offset_(0),
      data_size_(0),
      handle_(XE_ZAR_INVALID) {}

DiscZarchiveEntry::~DiscZarchiveEntry() = default;

std::unique_ptr<DiscZarchiveEntry> DiscZarchiveEntry::Create(
    Device* device, Entry* parent, const std::string_view name) {
  auto path = name;  // xe::utf8::join_guest_paths(parent->path(), name);
  auto entry = std::make_unique<DiscZarchiveEntry>(device, parent, path);
  return std::move(entry);
}

X_STATUS DiscZarchiveEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new DiscZarchiveFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

std::unique_ptr<MappedMemory> DiscZarchiveEntry::OpenMapped(
    MappedMemory::Mode mode, size_t offset, size_t length) {
  if (mode != MappedMemory::Mode::kRead) return nullptr;
  size_t absolute, count;
  if (!xe_file_span_slice(size_, 0, size_, offset, length, &absolute, &count))
    return nullptr;
  auto* device = dynamic_cast<DiscZarchiveDevice*>(device_);
  if (!device) return nullptr;
  const uint8_t* view = xe_zar_view(device->reader(), handle_, absolute, count);
  if (!view) return nullptr;
  // Read-only borrow of immutable archive storage. Entry/device must outlive
  // this wrapper; decoded cache slots are never exposed by xe_zar_view.
  return std::make_unique<MappedMemory>(const_cast<uint8_t*>(view), count);
}

bool DiscZarchiveEntry::DeleteEntryInternal(Entry* entry) { return false; }

}  // namespace vfs
}  // namespace xe

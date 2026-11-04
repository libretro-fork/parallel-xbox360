/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_devices/svod_container_file.h"
#include "xenia/vfs/devices/xcontent_devices/svod_container_entry.h"

namespace xe {
namespace vfs {

SvodContainerFile::SvodContainerFile(uint32_t file_access,
                                     SvodContainerEntry* entry)
    : XContentContainerFile(file_access, entry), entry_(entry) {}

SvodContainerFile::~SvodContainerFile() = default;

void SvodContainerFile::Destroy() { delete this; }

bool SvodContainerFile::CanRead(size_t offset, size_t length, size_t record_file) const {
  auto* files = entry_->files();
  if (!files) return false;
  auto file = files->find(record_file);
  return file != files->end() && bool(file->second);
}

size_t SvodContainerFile::Read(std::span<uint8_t> buffer, size_t offset,
                               size_t record_file) {
  auto* files = entry_->files();
  if (!files) return 0;
  auto file = files->find(record_file);
  if (file == files->end() || !file->second) return 0;
  size_t bytes_read = 0;
  if (!file->second->Read(offset, buffer.data(), buffer.size(), &bytes_read)) {
    return 0;
  }
  return bytes_read;
}

}  // namespace vfs
}  // namespace xe

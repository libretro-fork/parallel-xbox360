/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_XCONTENT_SVOD_CONTAINER_ENTRY_H_
#define XENIA_VFS_DEVICES_XCONTENT_SVOD_CONTAINER_ENTRY_H_

#include <memory>

#include "libretro/core_svod.h"
#include "xenia/vfs/devices/xcontent_container_entry.h"
#include "xenia/vfs/file.h"

namespace xe {
namespace vfs {
class XContentContainerDevice;

class SvodContainerEntry : public XContentContainerEntry {
 public:
  SvodContainerEntry(Device* device, Entry* parent, const std::string_view path,
                     xe_svod_files* files);
  ~SvodContainerEntry() override;

  static std::unique_ptr<SvodContainerEntry> Create(Device* device,
                                                    Entry* parent,
                                                    const std::string_view name,
                                                    xe_svod_files* files);

  xe_svod_files* files() const { return files_; }

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

 private:
  bool DeleteEntryInternal(Entry* entry) override;

  xe_svod_files* files_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_XCONTENT_SVOD_CONTAINER_ENTRY_H_

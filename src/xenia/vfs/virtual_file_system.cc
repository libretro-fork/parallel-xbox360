/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/virtual_file_system.h"

#include "devices/host_path_entry.h"
#include "libretro/core_content_extract.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/kernel/xfile.h"

namespace xe {
namespace vfs {

VirtualFileSystem::VirtualFileSystem() {}

VirtualFileSystem::~VirtualFileSystem() {
  // Delete all devices.
  // This will explode if anyone is still using data from them.
  Clear();
}

void VirtualFileSystem::Clear() {
  devices_.clear();
  symlinks_.clear();
}

bool VirtualFileSystem::RegisterDevice(std::unique_ptr<Device> device) {
  auto global_lock = global_critical_region_.Acquire();
  devices_.emplace_back(std::move(device));
  return true;
}

bool VirtualFileSystem::UnregisterDevice(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();
  for (auto it = devices_.begin(); it != devices_.end(); ++it) {
    if ((*it)->mount_path() == path) {
      XELOGD("Unregistered device: {}", (*it)->mount_path());
      devices_.erase(it);
      return true;
    }
  }
  return false;
}

Device* VirtualFileSystem::GetDevice(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();
  for (auto it = devices_.begin(); it != devices_.end(); ++it) {
    if ((*it)->mount_path() == path) {
      return (*it).get();
    }
  }
  return nullptr;
}

bool VirtualFileSystem::RegisterSymbolicLink(const std::string_view path,
                                             const std::string_view target) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = std::ranges::find_if(std::as_const(symlinks_), [&](const auto& s) {
    return xe::utf8::equal_case(path, s.first);
  });
  if (it != symlinks_.end()) {
    XELOGE("Trying to re-register already registered symbolic link: {} => {}",
           path, target);
    return false;
  }

  symlinks_.insert({std::string(path), std::string(target)});
  XELOGD("Registered symbolic link: {} => {}", path, target);

  return true;
}

bool VirtualFileSystem::UnregisterSymbolicLink(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = std::ranges::find_if(std::as_const(symlinks_), [&](const auto& s) {
    return xe::utf8::equal_case(path, s.first);
  });
  if (it == symlinks_.end()) {
    return false;
  }
  XELOGD("Unregistered symbolic link: {} => {}", it->first, it->second);

  symlinks_.erase(it);
  return true;
}

bool VirtualFileSystem::IsSymbolicLinkRegistered(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = std::ranges::find_if(std::as_const(symlinks_), [&](const auto& s) {
    return xe::utf8::equal_case(path, s.first);
  });

  return it != symlinks_.cend();
}

bool VirtualFileSystem::FindSymbolicLink(const std::string_view path,
                                         std::string& target) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = std::ranges::find_if(std::as_const(symlinks_), [&](const auto& s) {
    return xe::utf8::starts_with_case(path, s.first);
  });
  if (it == symlinks_.cend()) {
    return false;
  }
  target = (*it).second;
  return true;
}

bool VirtualFileSystem::ResolveSymbolicLink(const std::string_view path,
                                            std::string& result) {
  result = path;
  bool was_resolved = false;
  while (true) {
    auto it =
        std::ranges::find_if(std::as_const(symlinks_), [&](const auto& s) {
          return xe::utf8::starts_with_case(result, s.first);
        });
    if (it == symlinks_.cend()) {
      break;
    }
    // Found symlink!
    auto target_path = (*it).second;
    auto relative_path = result.substr((*it).first.size());
    result = xe::utf8::fix_guest_path_separators(target_path + relative_path);
    was_resolved = true;
  }
  return was_resolved;
}

namespace {

// A mount path matches only on a whole path component. Without this a device
// mounted at ...\Content also swallows ...\Content_Eng.
bool MountPathMatches(const std::string_view path,
                      const std::string_view mount_path) {
  if (mount_path.empty()) {
    return false;
  }
  const char last = mount_path.back();
  // Links name a \Device\Content\N\ root without the trailing separator.
  if (last == '\\' && path.size() + 1 == mount_path.size()) {
    return xe::utf8::equal_case(path, mount_path.substr(0, path.size()));
  }
  if (!xe::utf8::starts_with_case(path, mount_path)) {
    return false;
  }
  // Mounts ending in a delimiter already sit on a component boundary.
  if (last == '\\' || last == ':') {
    return true;
  }
  return path.size() == mount_path.size() || path[mount_path.size()] == '\\';
}

}  // namespace

Entry* VirtualFileSystem::ResolvePath(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();

  // Resolve relative paths
  auto normalized_path(xe::utf8::canonicalize_guest_path(path));

  // Resolve symlinks.
  std::string resolved_path;
  if (ResolveSymbolicLink(normalized_path, resolved_path)) {
    normalized_path = resolved_path;
  }

  // Find the device with the longest matching mount path. Devices nest, e.g.
  // \Device\Harddisk0\Partition1\Content lives inside \Device\Harddisk0, so the
  // most specific mount must win regardless of registration order.
  Device* device = nullptr;
  for (const auto& d : devices_) {
    if (MountPathMatches(normalized_path, d->mount_path()) &&
        (!device || d->mount_path().size() > device->mount_path().size())) {
      device = d.get();
    }
  }
  if (!device) {
    // Supress logging the error for ShaderDumpxe:\CompareBackEnds as this is
    // not an actual problem nor something we care about.
    if (path != "ShaderDumpxe:\\CompareBackEnds") {
      XELOGE("ResolvePath({}) failed - device not found", path);
    }
    return nullptr;
  }

  auto relative_path = normalized_path.substr(
      std::min(normalized_path.size(), device->mount_path().size()));
  return device->ResolvePath(relative_path);
}

Entry* VirtualFileSystem::CreatePath(const std::string_view path,
                                     uint32_t attributes) {
  auto path_parts = xe::utf8::split_path(path);
  if (path_parts.empty()) {
    return nullptr;
  }

  // A device mount spans several path components, not just the first.
  Entry* parent_entry = nullptr;
  size_t next_part = path_parts.size();
  while (next_part > 0) {
    const auto& part = path_parts[next_part - 1];
    const size_t prefix_length =
        static_cast<size_t>(part.data() + part.size() - path.data());
    parent_entry = ResolvePath(path.substr(0, prefix_length));
    if (parent_entry) {
      break;
    }
    --next_part;
  }
  if (!parent_entry) {
    return nullptr;
  }

  for (size_t i = next_part; i < path_parts.size(); ++i) {
    const bool is_last = i == path_parts.size() - 1;
    auto child_entry = parent_entry->CreateEntry(
        path_parts[i], is_last ? attributes : kFileAttributeDirectory);
    if (!child_entry) {
      return nullptr;
    }
    parent_entry = child_entry;
  }
  return parent_entry;
}

bool VirtualFileSystem::DeletePath(const std::string_view path) {
  auto entry = ResolvePath(path);
  if (!entry) {
    return false;
  }
  auto parent = entry->parent();
  if (!parent) {
    // Can't delete root.
    return false;
  }
  return parent->Delete(entry);
}

X_STATUS VirtualFileSystem::OpenFile(Entry* root_entry,
                                     const std::string_view path,
                                     FileDisposition creation_disposition,
                                     uint32_t desired_access, bool is_directory,
                                     bool is_non_directory, File** out_file,
                                     FileAction* out_action) {
  // TODO(gibbed): should 'is_directory' remain as a bool or should it be
  // flipped to a generic FileAttributeFlags?

  // Cleanup access.
  if (desired_access & FileAccess::kGenericRead) {
    desired_access |= FileAccess::kFileReadData;
  }
  if (desired_access & FileAccess::kGenericWrite) {
    desired_access |= FileAccess::kFileWriteData;
  }
  if (desired_access & FileAccess::kGenericAll) {
    desired_access |= FileAccess::kFileReadData | FileAccess::kFileWriteData;
  }

  // Lookup host device/parent path.
  // If no device or parent, fail.
  Entry* parent_entry = nullptr;
  Entry* entry = nullptr;

  auto base_path = xe::utf8::find_base_guest_path(path);
  if (!base_path.empty()) {
    parent_entry = !root_entry ? ResolvePath(base_path)
                               : root_entry->ResolvePath(base_path);
    if (!parent_entry) {
      *out_action = FileAction::kDoesNotExist;
      return X_STATUS_OBJECT_PATH_NOT_FOUND;
    }

    auto file_name = xe::utf8::find_name_from_guest_path(path);
    entry = parent_entry->GetChild(file_name);
  } else if (path.empty() && root_entry) {
    // An empty relative path is the root entry itself. Replacing it would
    // delete what the root handle still refers to.
    if (creation_disposition == FileDisposition::kSuperscede ||
        creation_disposition == FileDisposition::kOverwrite ||
        creation_disposition == FileDisposition::kOverwriteIf) {
      *out_action = FileAction::kDoesNotExist;
      return X_STATUS_ACCESS_DENIED;
    }
    entry = root_entry;
  } else {
    entry = !root_entry ? ResolvePath(path) : root_entry->GetChild(path);
  }

  if (entry) {
    if (entry->attributes() & kFileAttributeDirectory && is_non_directory) {
      return X_STATUS_FILE_IS_A_DIRECTORY;
    }

    // If the entry does not exist on the host then remove the cached entry
    if (parent_entry) {
      const xe::vfs::HostPathEntry* host_path =
          dynamic_cast<const xe::vfs::HostPathEntry*>(parent_entry);

      if (host_path) {
        auto const file_path = host_path->host_path() / entry->name();

        if (!std::filesystem::exists(file_path)) {
          // Remove cached entry
          entry->Delete();
          entry = nullptr;
        }
      }
    }
  }

  // Check if exists (if we need it to), or that it doesn't (if it shouldn't).
  switch (creation_disposition) {
    case FileDisposition::kOpen:
    case FileDisposition::kOverwrite:
      // Must exist.
      if (!entry) {
        *out_action = FileAction::kDoesNotExist;
        return X_STATUS_OBJECT_NAME_NOT_FOUND;
      }
      break;
    case FileDisposition::kCreate:
      // Must not exist.
      if (entry) {
        *out_action = FileAction::kExists;
        return X_STATUS_OBJECT_NAME_COLLISION;
      }
      break;
    default:
      // Either way, ok.
      break;
  }

  // Verify permissions.
  bool wants_write = desired_access & FileAccess::kFileWriteData ||
                     desired_access & FileAccess::kFileAppendData;
  if (wants_write && ((parent_entry && parent_entry->is_read_only()) ||
                      (entry && entry->is_read_only()))) {
    // Fail if read only device and wants write.
    // return X_STATUS_ACCESS_DENIED;
    // TODO(benvanik): figure out why games are opening read-only files with
    // write modes.
    XELOGW("Attempted to open read-only file/dir for write: {}", path);
    desired_access = FileAccess::kGenericRead | FileAccess::kFileReadData;
  }

  bool created = false;
  if (!entry) {
    // Remember that we are creating this new, instead of replacing.
    created = true;
    *out_action = FileAction::kCreated;
  } else {
    // May need to delete, if it exists.
    switch (creation_disposition) {
      case FileDisposition::kCreate:
        // Shouldn't be possible to hit this.
        assert_always();
        return X_STATUS_ACCESS_DENIED;
      case FileDisposition::kSuperscede:
        // Replace (by delete + recreate).
        if (!entry->Delete()) {
          return X_STATUS_ACCESS_DENIED;
        }
        entry = nullptr;
        *out_action = FileAction::kSuperseded;
        break;
      case FileDisposition::kOpen:
      case FileDisposition::kOpenIf:
        // Normal open.
        *out_action = FileAction::kOpened;
        break;
      case FileDisposition::kOverwrite:
      case FileDisposition::kOverwriteIf:
        // Overwrite (we do by delete + recreate).
        if (!entry->Delete()) {
          return X_STATUS_ACCESS_DENIED;
        }
        entry = nullptr;
        *out_action = FileAction::kOverwritten;
        break;
    }
  }
  if (!entry) {
    // Create if needed (either new or as a replacement).
    entry = CreatePath(
        path, !is_directory ? kFileAttributeNormal : kFileAttributeDirectory);
    if (!entry) {
      return X_STATUS_ACCESS_DENIED;
    }
  }

  // Open.
  auto result = entry->Open(desired_access, out_file);
  if (XFAILED(result)) {
    *out_action = FileAction::kDoesNotExist;
  }
  return result;
}

namespace {
struct ContentInput {
  Entry* entry = nullptr;
  File* file = nullptr;
  std::unique_ptr<MappedMemory> mapping;
  ~ContentInput() {
    if (mapping) mapping->Close();
    if (file) file->Destroy();
  }
};
}  // namespace

X_STATUS VirtualFileSystem::ExtractContentFile(
    Entry* entry, const std::filesystem::path& base_path) {
  if (!entry || (entry->attributes() & kFileAttributeDirectory))
    return X_STATUS_INVALID_PARAMETER;
  ContentInput input;
  input.entry = entry;
  xe_content_source source;
  source.opaque = &input;
  source.size = entry->size();
  source.view = [](void* opaque, uint64_t* available) -> const uint8_t* {
    ContentInput& input = *static_cast<ContentInput*>(opaque);
    *available = 0;
    if (!input.entry->can_map()) return nullptr;
    input.mapping = input.entry->OpenMapped(MappedMemory::Mode::kRead);
    if (!input.mapping) return nullptr;
    if (!input.mapping->data() || input.mapping->size() < input.entry->size()) {
      input.mapping->Close();
      input.mapping.reset();
      return nullptr;
    }
    *available = input.mapping->size();
    return input.mapping->data();
  };
  source.read = [](void* opaque, uint64_t offset, void* data,
                   size_t wanted, size_t* received) -> int {
    ContentInput& input = *static_cast<ContentInput*>(opaque);
    if (offset > SIZE_MAX) return 0;
    if (!input.file) {
      if (XFAILED(input.entry->Open(FileAccess::kFileReadData, &input.file)))
        return 0;
    }
    if (!input.file) return 0;
    return XFAILED(input.file->ReadSync(
        std::span<uint8_t>(static_cast<uint8_t*>(data), wanted),
        static_cast<size_t>(offset), received)) ? 0 : 1;
  };
#if XE_PLATFORM_WIN32
  const std::string directory = xe::path_to_utf8(base_path);
  const char* directory_view = directory.c_str();
#else
  const char* directory_view = base_path.c_str();
#endif
  return xe_content_extract_file(&source, directory_view,
      entry->name().data(), entry->name().size()) ?
      X_STATUS_SUCCESS : X_STATUS_UNSUCCESSFUL;
}
}  // namespace vfs
}  // namespace xe

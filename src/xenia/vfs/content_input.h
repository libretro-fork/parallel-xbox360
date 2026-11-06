/* Ownership adapter for synchronous C content transfers/parsers. */
#ifndef XENIA_VFS_CONTENT_INPUT_H_
#define XENIA_VFS_CONTENT_INPUT_H_
#include "libretro/core_content_input.h"
#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"
namespace xe {
namespace vfs {
class ContentInput {
 public:
  explicit ContentInput(Entry* entry)
      : entry_(entry), source_{this, entry->size(), View, Read} {}
  ContentInput(const ContentInput&) = delete;
  ContentInput& operator=(const ContentInput&) = delete;
  ~ContentInput() {
    if (mapping_) mapping_->Close();
    if (file_) file_->Destroy();
  }
  const xe_content_source* source() const { return &source_; }
  X_STATUS status() const { return status_; }
 private:
  static const uint8_t* View(void* opaque, uint64_t* available) {
    ContentInput& input = *static_cast<ContentInput*>(opaque);
    *available = 0;
    if (!input.entry_->can_map()) return nullptr;
    input.mapping_ = input.entry_->OpenMapped(MappedMemory::Mode::kRead);
    if (!input.mapping_) return nullptr;
    if (!input.mapping_->data() || input.mapping_->size() < input.source_.size) {
      input.mapping_->Close(); input.mapping_.reset(); return nullptr;
    }
    *available = input.mapping_->size(); return input.mapping_->data();
  }
  static int Read(void* opaque, uint64_t offset, void* data,
                  size_t wanted, size_t* received) {
    ContentInput& input = *static_cast<ContentInput*>(opaque);
    if (offset > SIZE_MAX) return 0;
    if (!input.file_) {
      input.status_ = input.entry_->Open(FileAccess::kFileReadData, &input.file_);
      if (XFAILED(input.status_)) return 0;
    }
    if (!input.file_) return 0;
    input.status_ = input.file_->ReadSync(
        std::span<uint8_t>(static_cast<uint8_t*>(data), wanted),
        static_cast<size_t>(offset), received);
    return XFAILED(input.status_) ? 0 : 1;
  }
  Entry* entry_;
  File* file_ = nullptr;
  std::unique_ptr<MappedMemory> mapping_;
  xe_content_source source_;
  X_STATUS status_ = X_STATUS_SUCCESS;
};
/* Declare after ContentInput: parser storage retires before its source. */
class ContentBuffer {
 public:
  ContentBuffer() = default;
  ContentBuffer(const ContentBuffer&) = delete;
  ContentBuffer& operator=(const ContentBuffer&) = delete;
  ~ContentBuffer() { xe_content_input_release(&input_); }
  int Acquire(const ContentInput& source, size_t minimum) {
    return xe_content_input_acquire(&input_, source.source(), minimum);
  }
  const uint8_t* data() const { return input_.data; }
  size_t size() const { return input_.size; }
 private:
  xe_content_input input_{};
};
}  // namespace vfs
}  // namespace xe
#endif

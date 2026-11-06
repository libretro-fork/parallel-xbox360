/* Minimal virtual-interface fixture; fault matrix and byte checks are C89. */
#include "core_content_fixture.h"
#define XE_CONTENT_ALLOC xe_test_buffer_allocate
#define XE_CONTENT_FREE xe_test_buffer_free
/* Isolate allocator-instrumented inline owner from shipping inline definitions
 * when this CPU-only fixture links the real core objects. Same header/body. */
#define ContentBuffer TestContentBuffer
#include "xenia/vfs/content_input.h"
#undef ContentBuffer
#include "xenia/vfs/device.h"
#include "xenia/vfs/devices/disc_zarchive_device.h"
using xe::X_STATUS;
namespace {
class TestDevice final : public xe::vfs::Device {
 public:
  TestDevice() : Device("test:") {}
  bool Initialize() override { return true; }
  void Dump(xe::StringBuffer*) override {}
  xe::vfs::Entry* ResolvePath(std::string_view) override { return nullptr; }
  const std::string& name() const override { return mount_path_; }
  uint32_t attributes() const override { return 0; }
  uint32_t component_name_max_length() const override { return 255; }
  uint32_t total_allocation_units() const override { return 1; }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 512; }
};
class TestFile final : public xe::vfs::File {
 public:
  TestFile(xe::vfs::Entry* entry, xe_test_content* test)
      : File(xe::vfs::FileAccess::kFileReadData, entry), test_(test) {}
  void Destroy() override { ++test_->destroyed; delete this; }
  X_STATUS ReadSync(std::span<uint8_t> bytes, size_t offset, size_t* received) override {
    if (test_->throw_read) throw 7;
    return xe_test_content_read(test_, offset, bytes.data(), bytes.size(), received) ?
        X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;
  }
  X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t*) override {
    return X_STATUS_ACCESS_DENIED;
  }
 private:
  xe_test_content* test_;
};
class TestMapping final : public xe::MappedMemory {
 public:
  explicit TestMapping(xe_test_content* test)
      : MappedMemory(const_cast<uint8_t*>(test->bytes), test->map == 3 ? 3 : test->size + 64),
        test_(test) { ++test_->mapped; }
  ~TestMapping() override { Close(); }
  void Close(uint64_t = 0) override {
    if (data_) { ++test_->unmapped; data_ = nullptr; size_ = 0; }
  }
 private:
  xe_test_content* test_;
};
class TestEntry final : public xe::vfs::Entry {
 public:
  TestEntry(TestDevice* device, xe_test_content* test)
      : Entry(device, nullptr, "game.xex"), test_(test) { size_ = test->size; }
  X_STATUS Open(uint32_t, xe::vfs::File** file) override {
    ++test_->opened;
    if (test_->map == 3 && test_->mapped != test_->unmapped) test_->invalid = 1;
    /* A backend which returns an owner together with failure must be cleaned. */
    if (test_->open_fault != 1) *file = new TestFile(this, test_);
    return test_->open_fault ? X_STATUS_ACCESS_DENIED : X_STATUS_SUCCESS;
  }
  bool can_map() const override { return test_->map != 0; }
  std::unique_ptr<xe::MappedMemory> OpenMapped(xe::MappedMemory::Mode,
      size_t, size_t) override {
    return test_->map == 2 ? nullptr : std::make_unique<TestMapping>(test_);
  }
 private:
  bool DeleteEntryInternal(Entry*) override { return false; }
  xe_test_content* test_;
};
}  // namespace
extern "C" int xe_test_content_bridge(xe_test_content* test) {
  TestDevice device;
  TestEntry entry(&device, test);
  try {
    xe::vfs::ContentInput source(&entry);
    xe::vfs::TestContentBuffer buffer;
    const int acquired = buffer.Acquire(source, 4);
    if (!acquired) return XFAILED(source.status()) ? 0 : -1;
    if (acquired < 0) return -1;
    xe_test_content_consume(test, buffer.data(), buffer.size());
    if (test->throw_consumer) throw 8;
    return 1;
  } catch (int) { return 2; }
}
extern "C" int xe_test_archive_bridge(const char* path, int compressed) {
  xe::vfs::DiscZarchiveDevice device("test:", xe::to_path(path));
  if (!device.Initialize()) return 0;
  auto* entry = device.ResolvePath("raw");
  if (!entry || !entry->can_map() || entry->size() != 131072) return 0;
  if (entry->OpenMapped(xe::MappedMemory::Mode::kReadWrite) ||
      entry->OpenMapped(xe::MappedMemory::Mode::kRead, entry->size() + 1, 0)) return 0;
  const uint8_t* view = xe_zar_view(device.reader(),
      xe_zar_lookup(device.reader(), "raw", 3, 1, 0), 0, entry->size());
  {
    xe::vfs::ContentInput source(entry);
    xe::vfs::TestContentBuffer buffer;
    if (buffer.Acquire(source, 4) != 1) return 0;
    if (compressed ? view != nullptr : buffer.data() != view || !view) return 0;
    if (!xe_test_archive_bytes(buffer.data(), buffer.size(), compressed)) return 0;
    auto slice = entry->OpenMapped(xe::MappedMemory::Mode::kRead, 65539, 11);
    if (compressed ? slice != nullptr : !slice || slice->size() != 11 ||
        slice->data() != view + 65539) return 0;
    auto tail = entry->OpenMapped(xe::MappedMemory::Mode::kRead, 131069, 32);
    if (compressed ? tail != nullptr : !tail || tail->size() != 3 ||
        tail->data() != view + 131069) return 0;
  }
  return 1;
}

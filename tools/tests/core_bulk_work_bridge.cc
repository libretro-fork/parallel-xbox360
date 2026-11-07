// Only native exception/RAII glue is C++; scheduling is the actual C89 module.
#include "xenia/base/bulk_work.h"
#include "libretro/core_gpu_worklist.h"
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <features/features_cpu.h>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main() {
  xe_gpu_worklist list = {};
  std::unique_ptr<xe_gpu_worklist, decltype(&xe_gpu_worklist_close)> storage(
      &list, &xe_gpu_worklist_close);
  CHECK(xe_gpu_worklist_init(&list, 4096, sizeof(unsigned)));
  for (unsigned i = 0; i < 4096; ++i)
    new (xe_gpu_worklist_append(&list)) unsigned(i);
  auto* items = static_cast<unsigned*>(list.data);
  xe_bulk_work work;
  CHECK(xe_bulk_work_init(&work, list.count));
  auto success = [&]() {
    size_t index, completed = 0;
    while (xe_bulk_work_claim(&work, &index)) {
      items[index] ^= 0xDEADu;
      ++completed;
    }
    xe_bulk_work_complete(&work, completed);
  };
  xe::RunBulkWork(work, 6, success, "Bulk bridge");
  CHECK(xe_bulk_work_completed(&work) == 4096);
  for (unsigned i = 0; i < 4096; ++i) CHECK(items[i] == (i ^ 0xDEADu));

  // Every live callback holds a stack owner. Both caller and worker failures
  // have to finish all owners before rethrow or freeing borrowed item storage.
  for (unsigned mode = 0; mode < 3; ++mode) {
    retro_atomic_int_t active, entered;
    retro_atomic_int_init(&active, 0);
    retro_atomic_int_init(&entered, 0);
    CHECK(xe_bulk_work_init(&work, list.count));
    auto failure = [&]() {
      struct Owner {
        retro_atomic_int_t* active;
        explicit Owner(retro_atomic_int_t* p) : active(p) {
          retro_atomic_fetch_add_int(active, 1);
        }
        ~Owner() { retro_atomic_fetch_add_int(active, -1); }
      } owner(&active);
      retro_atomic_fetch_add_int(&entered, 1);
      struct Setup {
        explicit Setup(unsigned mode) {
          if (mode == 1) throw std::runtime_error("translator setup failure");
        }
      } setup(mode);
      if (mode == 2) {
        const int64_t start = cpu_features_get_time_usec();
        while (retro_atomic_load_acquire_int(&entered) != 7)
          CHECK(cpu_features_get_time_usec() - start < 10000000);
        throw std::runtime_error("concurrent setup failure");
      }
      size_t index;
      while (xe_bulk_work_claim(&work, &index)) {
        if (index == 3) throw std::runtime_error("shader analysis failure");
        items[index] = 42;
      }
    };
    bool caught = false;
    try { xe::RunBulkWork(work, 6, failure, "Bulk failure"); }
    catch (const std::runtime_error&) { caught = true; }
    CHECK(caught && retro_atomic_load_acquire_int(&active) == 0);
    CHECK(retro_atomic_load_acquire_int(&entered) > 0);
    size_t index;
    CHECK(!xe_bulk_work_claim(&work, &index));
  }
  // The same borrowed allocation can be reused after an aborted batch.
  CHECK(xe_bulk_work_init(&work, list.count));
  xe::RunBulkWork(work, 0, success, "Bulk retry");
  CHECK(xe_bulk_work_completed(&work) == 4096);
  std::puts("bulk native bridge: borrowed storage, constructor/job exceptions, joined owners and reuse PASS");
}

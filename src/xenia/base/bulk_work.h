// Native C++ callback/exception boundary for the C89 bulk worker. All worker
// accesses finish before the caller observes the first exception or unwinds.
#ifndef XENIA_BASE_BULK_WORK_H_
#define XENIA_BASE_BULK_WORK_H_
#include <exception>
#include "libretro/core_bulk_work.h"

namespace xe {
template <typename Function>
void RunBulkWork(xe_bulk_work& work, size_t background_workers,
                 Function& function, const char* name) {
  struct Invocation {
    xe_bulk_work* work;
    Function* function;
    std::exception_ptr error;
    static void Run(void* opaque) noexcept {
      auto& self = *static_cast<Invocation*>(opaque);
      if (retro_atomic_load_acquire_int(&self.work->failed)) return;
      try {
        (*self.function)();
      } catch (...) {
        if (xe_bulk_work_fail(self.work)) self.error = std::current_exception();
      }
    }
  } invocation{&work, &function, std::exception_ptr()};
  xe_bulk_work_run(background_workers, work.count, &Invocation::Run,
                   &invocation, name);
  if (invocation.error) std::rethrow_exception(invocation.error);
}
}  // namespace xe
#endif

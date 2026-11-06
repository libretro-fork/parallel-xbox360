/* C++ interop only: exercise the production synchronous presenter call chain.
 * The parent fixture compiles against its own complete presenter sources. */
#include "xenia/ui/presenter.h"
#include "core_pipeline_bench_profile.h"
#include <new>
#include <cstdlib>
#include <string_view>

void* operator new(size_t bytes) {
  if (void* p = xe_bench_malloc(bytes ? bytes : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { xe_bench_free(p); }
void operator delete[](void* p) noexcept { xe_bench_free(p); }
void operator delete(void* p, size_t) noexcept { xe_bench_free(p); }
void operator delete[](void* p, size_t) noexcept { xe_bench_free(p); }
namespace xe { void FatalError(std::string_view) { std::abort(); } }

#ifdef XE_PRESENTER_PARENT
#define XE_CALL_REFRESH(fn) RefreshGuestOutput(256, 1, 16, 9, fn)
#else
#define XE_CALL_REFRESH(fn) RefreshGuestOutput(256, 1, 16, 9, \
  [](void* data, GuestOutputRefreshContext& context) { \
    return (*static_cast<decltype(fn)*>(data))(context); }, &fn)
#endif

class BenchPresenter final : public xe::ui::Presenter {
 public:
#ifdef XE_PRESENTER_PARENT
  BenchPresenter() : Presenter(nullptr) {}
#else
  BenchPresenter() : Presenter() {}
#endif
  bool CaptureGuestOutput(xe::ui::RawImage&) override { return false; }
  bool Run(unsigned words, unsigned value) {
    if (words == 4) {
      struct { uintptr_t words[4]; } state = {{value, 1, 2, 3}};
      auto callback = [state](GuestOutputRefreshContext& context) {
        context.SetIs8bpc(state.words[0] != 0);
        return state.words[1] + state.words[2] == state.words[3];
      };
      static_assert(sizeof(callback) == 4 * sizeof(uintptr_t));
      return XE_CALL_REFRESH(callback);
    }
    struct { uintptr_t words[5]; } state = {{value, 1, 2, 3, 4}};
    auto callback = [state](GuestOutputRefreshContext& context) {
      context.SetIs8bpc(state.words[0] != 0);
      return state.words[1] + state.words[4] == state.words[2] + state.words[3];
    };
    static_assert(sizeof(callback) == 5 * sizeof(uintptr_t));
    return XE_CALL_REFRESH(callback);
  }
 private:
  bool RefreshGuestOutputImpl(uint32_t, uint32_t, uint32_t,
#ifdef XE_PRESENTER_PARENT
      std::function<bool(GuestOutputRefreshContext&)> callback,
#else
      GuestOutputRefresher callback, void* data,
#endif
      bool& is_8bpc) override {
    struct Context : GuestOutputRefreshContext {
      explicit Context(bool& value) : GuestOutputRefreshContext(value) {}
    } context(is_8bpc);
#ifdef XE_PRESENTER_PARENT
    return callback(context);
#else
    return callback(data, context);
#endif
  }
};
extern "C" void* xe_presenter_bench_new(void) { return new BenchPresenter(); }
extern "C" void xe_presenter_bench_free(void* p) { delete static_cast<BenchPresenter*>(p); }
extern "C" int xe_presenter_bench_run(void* p, unsigned words, unsigned value) {
  return static_cast<BenchPresenter*>(p)->Run(words, value);
}

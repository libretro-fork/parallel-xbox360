/* Minimal C++ bridge to exercise the production guest-output mailbox. */
#include "xenia/ui/presenter.h"
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace xe {
/* Standalone fixture has no logger; a fatal error must fail the test. */
void FatalError(std::string_view) { std::abort(); }
}

extern "C" void xe_test_presenter_fill(unsigned* pixels, unsigned value);

class TestPresenter final : public xe::ui::Presenter {
 public:
  TestPresenter() : Presenter() {}
  bool CaptureGuestOutput(xe::ui::RawImage&) override { return false; }
  bool Refresh(unsigned value, bool active, bool fail) {
    value_ = value;
    fail_ = fail;
    return RefreshGuestOutput(active ? 256 : 0, 1, 16, 9,
                             [](void*, GuestOutputRefreshContext&) { return true; }, nullptr);
  }
  bool Capture(unsigned* pixels) {
    uint32_t index;
    auto lock = ConsumeGuestOutput(index);
    if (index == UINT32_MAX) return false;
    std::memcpy(pixels, pixels_[index], sizeof(pixels_[index]));
    return true;
  }
 private:
  bool RefreshGuestOutputImpl(uint32_t index, uint32_t, uint32_t,
      GuestOutputRefresher refresher, void* data, bool& is_8bpc) override {
    struct Context : GuestOutputRefreshContext {
      explicit Context(bool& value) : GuestOutputRefreshContext(value) {}
    } context(is_8bpc);
    if (!refresher(data, context)) return false;
    if (fail_) return false;
    xe_test_presenter_fill(pixels_[index], value_);
    return true;
  }
 public:
  bool NestedCapture() {
    uint32_t index;
    auto lease = ConsumeGuestOutput(index);
    if (index == UINT32_MAX) return false;
    auto moved = std::move(lease);
    unsigned pixels[256];
    return !Capture(pixels);
  }
 private:
  unsigned pixels_[kGuestOutputMailboxSize][256] = {};
  unsigned value_ = 0;
  bool fail_ = false;
};

extern "C" void* xe_test_presenter_new(void) { return new TestPresenter(); }
extern "C" void xe_test_presenter_free(void* p) {
  delete static_cast<TestPresenter*>(p);
}
extern "C" int xe_test_presenter_refresh(void* p, unsigned value,
                                         int active, int fail) {
  return static_cast<TestPresenter*>(p)->Refresh(value, active != 0, fail != 0);
}
extern "C" int xe_test_presenter_capture(void* p, unsigned* pixels) {
  return static_cast<TestPresenter*>(p)->Capture(pixels);
}

extern "C" int xe_test_presenter_nested(void* p) {
  return static_cast<TestPresenter*>(p)->NestedCapture();
}

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_PRESENTER_H_
#define XENIA_UI_PRESENTER_H_
#include "xenia/base/byte_order.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "libretro/core_video_mailbox.h"
namespace xe {
namespace ui {
struct RawImage {
  uint32_t width = 0;
  uint32_t height = 0;
  size_t stride = 0;
  std::vector<uint8_t> data;
};
// Owns guest output images for frontend capture. No window or host presentation.
class Presenter {
 public:
  class GuestOutputRefreshContext {
   public:
    GuestOutputRefreshContext(const GuestOutputRefreshContext& context) =
        delete;
    GuestOutputRefreshContext& operator=(
        const GuestOutputRefreshContext& context) = delete;
    virtual ~GuestOutputRefreshContext() = default;

    // Sets whether the source actually has no more than 8 bits of precision
    // (though the image provided by the refresher may still have a higher
    // storage precision). If never called, assuming it's false.
    void SetIs8bpc(bool is_8bpc) { is_8bpc_out_ref_ = is_8bpc; }

   protected:
    GuestOutputRefreshContext(bool& is_8bpc_out_ref)
        : is_8bpc_out_ref_(is_8bpc_out_ref) {
      is_8bpc_out_ref = false;
    }

   private:
    bool& is_8bpc_out_ref_;
  };


  // Invoked synchronously; neither the function nor its context is retained.
  using GuestOutputRefresher = bool (*)(void*, GuestOutputRefreshContext&);

  Presenter(const Presenter&) = delete;
  Presenter& operator=(const Presenter&) = delete;
  virtual ~Presenter() = default;
  bool RefreshGuestOutput(uint32_t frontbuffer_width, uint32_t frontbuffer_height,
                          uint32_t display_aspect_ratio_x,
                          uint32_t display_aspect_ratio_y,
                          GuestOutputRefresher refresher, void* refresher_data);
  virtual bool CaptureGuestOutput(RawImage& image_out) = 0;
 protected:
  static uint32_t Packed10bpcRGBTo8bpcBytes(uint32_t rgb10) {
    // Conversion almost according to the Direct3D 10+ rules (unorm > float >
    // unorm), but with one multiplication rather than separate division and
    // multiplication - the results are the same for unorm10 to unorm8.
    if constexpr (std::endian::native == std::endian::big) {
      return (uint32_t(float(rgb10 & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
              << 24) |
             (uint32_t(float((rgb10 >> 10) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
              << 16) |
             (uint32_t(float((rgb10 >> 20) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
              << 8) |
             uint32_t(0xFF);
    }
    return uint32_t(float(rgb10 & 0x3FF) * (255.0f / 1023.0f) + 0.5f) |
           (uint32_t(float((rgb10 >> 10) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
            << 8) |
           (uint32_t(float((rgb10 >> 20) & 0x3FF) * (255.0f / 1023.0f) + 0.5f)
            << 16) |
           (uint32_t(0xFF) << 24);
  }

  Presenter() {
    xe_video_mailbox_init(&guest_output_mailbox_);
  }
  static constexpr uint32_t kGuestOutputMailboxSize = 3;
  struct GuestOutputProperties {
    // At least any value being 0 here means the guest output is disabled for
    // this frame.
    uint32_t frontbuffer_width;
    uint32_t frontbuffer_height;
    // Guest display aspect ratio numerator and denominator (both 16:9 and
    // 1280:720 kinds of values are accepted).
    uint32_t display_aspect_ratio_x;
    uint32_t display_aspect_ratio_y;
    bool is_8bpc;

    GuestOutputProperties() { SetToInactive(); }

    bool IsActive() const {
      return frontbuffer_width && frontbuffer_height &&
             display_aspect_ratio_x && display_aspect_ratio_y;
    }

    void SetToInactive() {
      frontbuffer_width = 0;
      frontbuffer_height = 0;
      display_aspect_ratio_x = 0;
      display_aspect_ratio_y = 0;
      is_8bpc = false;
    }
  };


  virtual bool RefreshGuestOutputImpl(uint32_t mailbox_index,
      uint32_t frontbuffer_width, uint32_t frontbuffer_height,
      GuestOutputRefresher refresher, void* refresher_data,
      bool& is_8bpc_out) = 0;
  // A failed claim produces no frame; the frontend may repeat its last one.
  // Keep the lease while taking any slot-owned references, including on errors.
  class GuestOutputLease {
   public:
    explicit GuestOutputLease(xe_video_mailbox* mailbox, uint32_t& index)
        : mailbox_(mailbox) {
      index = xe_video_mailbox_read_begin(mailbox);
      if (index == UINT32_MAX) mailbox_ = nullptr;
    }
    GuestOutputLease(const GuestOutputLease&) = delete;
    GuestOutputLease& operator=(const GuestOutputLease&) = delete;
    GuestOutputLease(GuestOutputLease&& other) noexcept : mailbox_(other.mailbox_) {
      other.mailbox_ = nullptr;
    }
    ~GuestOutputLease() {
      if (mailbox_) xe_video_mailbox_read_end(mailbox_);
    }
   private:
    xe_video_mailbox* mailbox_;
  };
  [[nodiscard]] GuestOutputLease ConsumeGuestOutput(
      uint32_t& mailbox_index_or_max_if_inactive_out);
 private:
  // One producer; consumer leases protect slot snapshots without blocking.
  xe_video_mailbox guest_output_mailbox_;
  std::array<GuestOutputProperties, kGuestOutputMailboxSize> guest_output_properties_;
  bool guest_output_active_last_refresh_ = false;
};
}  // namespace ui
}  // namespace xe
#endif

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/presenter.h"
namespace xe {
namespace ui {
bool Presenter::RefreshGuestOutput(
    uint32_t frontbuffer_width, uint32_t frontbuffer_height,
    uint32_t display_aspect_ratio_x, uint32_t display_aspect_ratio_y,
    GuestOutputRefresher refresher, void* refresher_data) {
  GuestOutputProperties& writable_properties =
      guest_output_properties_[guest_output_mailbox_.writable];
  writable_properties.frontbuffer_width = frontbuffer_width;
  writable_properties.frontbuffer_height = frontbuffer_height;
  writable_properties.display_aspect_ratio_x = display_aspect_ratio_x;
  writable_properties.display_aspect_ratio_y = display_aspect_ratio_y;
  writable_properties.is_8bpc = false;
  bool is_active = writable_properties.IsActive();
  if (is_active) {
    if (!RefreshGuestOutputImpl(guest_output_mailbox_.writable,
                                frontbuffer_width, frontbuffer_height,
                                refresher, refresher_data, writable_properties.is_8bpc)) {
      // If failed to refresh, don't send the currently writable image to the
      // mailbox as it may be in an undefined state. Don't disable the guest
      // output either though because the failure may be something transient.
      return false;
    }
    guest_output_active_last_refresh_ = true;
  } else {
    // Request presenting a blank image if there was a true image previously,
    // but not now.
    if (!guest_output_active_last_refresh_) {
      return false;
    }
    guest_output_active_last_refresh_ = false;
  }

  xe_video_mailbox_publish(&guest_output_mailbox_);

  return is_active;
}

Presenter::GuestOutputLease Presenter::ConsumeGuestOutput(
    uint32_t& mailbox_index_or_max_if_inactive_out) {
  uint32_t mailbox_index;
  GuestOutputLease consumer_lease(&guest_output_mailbox_, mailbox_index);
  mailbox_index_or_max_if_inactive_out = UINT32_MAX;
  if (mailbox_index == UINT32_MAX) return consumer_lease;
  // Give the current acquired image to the caller, or UINT32_MAX if it's
  // inactive.
  const GuestOutputProperties& properties =
      guest_output_properties_[mailbox_index];
  mailbox_index_or_max_if_inactive_out =
      properties.IsActive() ? mailbox_index : UINT32_MAX;
  return consumer_lease;
}

}  // namespace ui
}  // namespace xe

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_ui.h"

#include <algorithm>

#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/title_id_utils.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xam/xam_private.h"

DECLARE_int32(license_mask);

namespace xe {
namespace kernel {
namespace xam {
// Results use the kernel dispatcher; UI close notifications follow frontend time.
X_RESULT xeXamDispatchWithoutDialog(std::function<X_RESULT()> run_callback,
                                    uint32_t overlapped) {
  auto* state = kernel_state()->xam_state();
  xe_ui_request* request = nullptr;
  int accepted = xe_ui_queue_begin(state->ui_notifications(), &request);
  if (accepted <= 0)
    return accepted == 0 ? X_ERROR_ACCESS_DENIED : X_ERROR_NOT_ENOUGH_MEMORY;
  auto post = [state, request]() {
    xe_ui_queue_end(state->ui_notifications(), request);
  };
  if (!overlapped) {
    X_RESULT result = run_callback();
    post();
    return result;
  }
  kernel_state()->CompleteOverlappedDeferred(run_callback, overlapped, nullptr, post);
  return X_ERROR_IO_PENDING;
}

// No window (the libretro core): a dialog that only shows something - the
// achievements list, the gamer card - is not made at all. Its constructor
// loads what it draws through the drawer, which is not there; the game is
// told a dialog opened and closed, as when the user dismisses it at once.
static bool xeXamSkipDisplayDialog(const char* name) {
  XELOGI("{}: dismissed by the libretro frontend", name);
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  return true;
}

std::function<void(const std::string& title, const std::string& answer)>
    xam_message_box_notice;

static dword_result_t XamShowMessageBoxUi(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!result_ptr || (button_count && !button_ptrs)) return X_ERROR_INVALID_PARAMETER;
  std::string title = title_ptr ? xe::to_utf8(title_ptr.value()) : "";
  std::string text = text_ptr ? xe::to_utf8(text_ptr.value()) : "";

  std::vector<std::string> buttons;
  for (uint32_t i = 0; i < button_count; ++i) {
    uint32_t button_ptr = button_ptrs[i];
    auto button = xe::load_and_swap<std::u16string>(
        kernel_state()->memory()->TranslateVirtual(button_ptr));

    if (!button.empty()) {
      buttons.push_back(xe::to_utf8(button));
    }
  }

  if (buttons.empty()) {
    buttons.push_back("OK");
  }

  // In the libretro core, with no window, the box is answered at once, the
  // way the player would go on: a button that goes on offline when there is
  // one (a core has no Xbox LIVE; "Connect to Xbox LIVE" only asks again -
  // Minecraft's "Gamer profile not online" at Play), otherwise the active
  // button when it is not a way back (Cancel, Back, No) - the game's own
  // default; "Select a storage device" before it can't be answered without a
  // window, so Sonic's Ultimate Genesis Collection asked "Load Scores and
  // Settings" again forever - otherwise the first that is not a way back: the
  // box asks to confirm something the player chose to do, like deleting a
  // save (NNshi); the active button when every button is one. The log and RetroArch's on-screen message say
  // what was asked and answered.
  xe_ui_choice choice;
  xe_ui_choice_begin(&choice, static_cast<unsigned>(active_button));
  unsigned i;
  for (i = 0; i < buttons.size(); ++i)
    xe_ui_choice_add(&choice, buttons[i].c_str(), i);
  uint32_t answer = xe_ui_choice_result(&choice, static_cast<unsigned>(buttons.size()));
  XELOGI("XamShowMessageBoxUI: \"{}\" - \"{}\"; answered {} [{}]; flags {:08X}",
         title, text, answer, buttons[answer], static_cast<uint32_t>(flags));
  if (xam_message_box_notice) xam_message_box_notice(title, buttons[answer]);

  return xeXamDispatchWithoutDialog([result_ptr, answer, flags]() -> X_RESULT {
    if (flags & XMBox_PASSCODEMODE || flags & XMBox_VERIFYPASSCODEMODE) {
      std::memset(result_ptr, 0, sizeof(MESSAGEBOX_RESULT));
      return X_ERROR_FUNCTION_FAILED;
    }
    result_ptr->ButtonPressed = answer;
    return X_ERROR_SUCCESS;
  }, overlapped);
}

dword_result_t XamIsUIActive_entry() {
  return kernel_state()->xam_state()->IsUIActive();
}
DECLARE_XAM_EXPORT2(XamIsUIActive, kUI, kImplemented, kHighFrequency);

// https://www.se7ensins.com/forums/threads/working-xshowmessageboxui.844116/
dword_result_t XamShowMessageBoxUI_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUI, kUI, kImplemented);

dword_result_t XamShowMessageBoxUIEx_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, dword_t unknown_unused,
    pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUIEx, kUI, kImplemented);

dword_result_t XNotifyQueueUI_entry(dword_t exnq, dword_t dwUserIndex,
                                    qword_t qwAreas,
                                    lpu16string_t displayText_ptr,
                                    lpvoid_t contextData) {
  XELOGI("XNotifyQueueUI: {}", displayText_ptr ? xe::to_utf8(displayText_ptr.value()) : "");
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XNotifyQueueUI, kUI, kSketchy);

// https://www.se7ensins.com/forums/threads/release-how-to-use-xshowkeyboardui-release.906568/
dword_result_t XamShowKeyboardUI_entry(
    dword_t user_index, dword_t flags, lpu16string_t default_text,
    lpu16string_t title, lpu16string_t description, lpu16string_t buffer,
    dword_t buffer_length, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!buffer || !buffer_length || !overlapped) return X_ERROR_INVALID_PARAMETER;
  std::string value = default_text ? xe::to_utf8(default_text.value()) : "";
  if (value.empty()) {
    auto* profiles = kernel_state()->xam_state()->profile_manager();
    auto profile = profiles->GetProfile(static_cast<uint8_t>(user_index));
    if (!profile) profile = profiles->GetProfile(static_cast<uint8_t>(0));
    if (profile) value = profile->name();
  }
  auto text = xe::to_utf16(value);
  return xeXamDispatchWithoutDialog(
      [buffer, buffer_length, text = std::move(text)]() -> X_RESULT {
        string_util::copy_and_swap_truncating(buffer, text, buffer_length);
        return X_ERROR_SUCCESS;
      }, overlapped);
}
DECLARE_XAM_EXPORT1(XamShowKeyboardUI, kUI, kImplemented);

dword_result_t XamShowDeviceSelectorUI_entry(
    dword_t user_index, dword_t content_type, dword_t content_flags,
    qword_t total_requested, lpdword_t device_id_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {

  if (!overlapped) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if ((user_index >= XUserMaxUserCount && user_index != XUserIndexAny) ||
      (content_flags & 0x83F00008) != 0 || !device_id_ptr) {
    XOverlappedSetExtendedError(overlapped, X_ERROR_INVALID_PARAMETER);
    return X_ERROR_INVALID_PARAMETER;
  }

  std::vector<const DummyDeviceInfo*> devices = ListStorageDevices();

  return xeXamDispatchWithoutDialog(
      [device_id_ptr, devices]() -> X_RESULT {
        if (devices.empty()) {
          return X_ERROR_CANCELLED;
        }

        const DummyDeviceInfo* device_info = devices.front();
        *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
        return X_ERROR_SUCCESS;
      },
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowDeviceSelectorUI, kUI, kImplemented);

void XamShowDirtyDiscErrorUI_entry(dword_t user_index,
                                   const ppc_context_t& context) {
  XELOGE("XamShowDirtyDiscErrorUI: title reported a disc read error, lr {:08X}", uint32_t(context->lr));
  kernel_state()->ExitToDashboard();
}
DECLARE_XAM_EXPORT1(XamShowDirtyDiscErrorUI, kUI, kImplemented);

dword_result_t XamShowPartyUI_entry(dword_t user_index) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowPartyUI, kNone, kStub);

dword_result_t XamShowCommunitySessionsUI_entry(unknown_t r3, unknown_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowCommunitySessionsUI, kNone, kStub);

// this is supposed to do a lot more, calls another function that triggers some
// cbs
dword_result_t XamSetDashContext_entry(dword_t value,
                                       const ppc_context_t& ctx) {
  ctx->kernel_state->dash_context_ = value;
  kernel_state()->BroadcastNotification(kXNotificationSystemDashContextChanged,
                                        0);
  return 0;
}

DECLARE_XAM_EXPORT1(XamSetDashContext, kNone, kImplemented);

dword_result_t XamGetDashContext_entry(const ppc_context_t& ctx) {
  return ctx->kernel_state->dash_context_;
}

DECLARE_XAM_EXPORT1(XamGetDashContext, kNone, kImplemented);

// https://gitlab.com/GlitchyScripts/xlivelessness/-/blob/master/xlivelessness/xlive/xdefs.hpp?ref_type=heads#L1235
dword_result_t XamShowMarketplaceUIEx_entry(dword_t user_index, dword_t ui_type,
                                            qword_t offer_id,
                                            dword_t offer_type,
                                            dword_t content_category,
                                            unknown_t unk6, unknown_t unk7,
                                            dword_t title_id) {
  if (user_index >= XUserMaxUserCount || ui_type >= MaxEntrypoint) return X_ERROR_INVALID_PARAMETER;
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) return X_ERROR_NO_SUCH_USER;
  return xeXamDispatchWithoutDialog([ui_type, offer_id]() -> X_RESULT {
    if (ui_type == 1 && offer_id == ((uint64_t(kernel_state()->title_id()) << 32) | 1ull)) {
      cvars::license_mask = 1;
      kernel_state()->BroadcastNotification(kXNotificationLiveContentInstalled, 0);
    }
    return X_ERROR_SUCCESS;
  }, 0);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUIEx, kUI, kSketchy);

dword_result_t XamShowMarketplaceUI_entry(dword_t user_index, dword_t ui_type,
                                          qword_t offer_id, dword_t offer_type,
                                          dword_t content_category,
                                          dword_t title_id) {
  return XamShowMarketplaceUIEx_entry(user_index, ui_type, offer_id, offer_type,
                                      content_category, 0, 0, title_id);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUI, kUI, kSketchy);

dword_result_t XamShowMarketplaceDownloadItemsUI_entry(
    dword_t user_index, dword_t ui_type, lpqword_t offers, dword_t num_offers,
    lpdword_t hresult_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (user_index >= XUserMaxUserCount || !offers || num_offers > 6 ||
      (ui_type != X_MARKETPLACE_DOWNLOAD_ITEMS_ENTRYPOINTS::FREEITEMS &&
       ui_type != X_MARKETPLACE_DOWNLOAD_ITEMS_ENTRYPOINTS::PAIDITEMS)) return X_ERROR_INVALID_PARAMETER;
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    if (!overlapped) return X_ERROR_NO_SUCH_USER;
    kernel_state()->CompleteOverlappedImmediate(overlapped, X_ERROR_NO_SUCH_USER);
    return X_ERROR_IO_PENDING;
  }
  return xeXamDispatchWithoutDialog([hresult_ptr]() -> X_RESULT {
    if (hresult_ptr) *hresult_ptr = X_E_SUCCESS;
    return X_ERROR_SUCCESS;
  }, overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceDownloadItemsUI, kUI, kSketchy);

dword_result_t XamShowForcedNameChangeUI_entry(dword_t user_index) {
  // Changes from 6 to 8 past NXE
  return XamShowMarketplaceUIEx_entry(user_index, 6, 0, 0xffffffff, 0, 0, 0, 0);
}
DECLARE_XAM_EXPORT1(XamShowForcedNameChangeUI, kUI, kImplemented);

X_RESULT xeXamShowSigninUI(uint32_t user_index, uint32_t users_needed,
                           uint32_t flags) {

  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger sign in notification.
  if (users_needed != 1 && users_needed != 2 && users_needed != 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (kernel_state()->xam_state()->IsUIActive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  // No window (the libretro core): there is nobody to pick a profile, and the
  // core has signed one in already. Answer as the dialog's OK would - the
  // games that sit and wait for a sign-in change get one for the profiles
  // that are signed in - instead of a dialog that just goes away.

  auto* profiles = kernel_state()->xam_state()->profile_manager();
  uint32_t signed_in = 0;
  for (uint8_t slot = 0; slot < XUserMaxUserCount; ++slot) {
    if (profiles->GetProfile(slot)) {
      signed_in |= 1u << slot;
    }
  }
  XELOGI("XamShowSigninUI without a window: {} user(s) needed, signed in "
         "mask {:X}",
         users_needed, signed_in);
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  kernel_state()->BroadcastNotification(kXNotificationSystemSignInChanged,
                                        signed_in);
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  return X_ERROR_SUCCESS;

}

X_RESULT xeXamShowCreateProfileUIEx(uint32_t user_index, dword_t flag,
                                    char* unkn2_ptr) {
  if (kernel_state()->xam_state()->IsUIActive()) return X_ERROR_ACCESS_DENIED;
  return X_ERROR_SUCCESS;
}

dword_result_t XamShowSigninUI_entry(dword_t users_needed, dword_t flags) {
  return xeXamShowSigninUI(XUserIndexAny, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUI, kUserProfiles, kImplemented);

dword_result_t XamShowSigninUIEx_entry(
    dword_t users_needed, dword_t flags,
    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  X_RESULT result = xeXamShowSigninUI(XUserIndexAny, users_needed, flags);
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamShowSigninUIEx, kUserProfiles, kSketchy);

dword_result_t XamShowNuiSigninUI_entry(dword_t unk, dword_t user_index,
                                        dword_t flags) {
  uint32_t users_needed = 1;
  uint32_t sent_flags = flags | static_cast<uint32_t>(SigninUiFlags::NUI);
  // xeXamNuiHudCheck(unk) = success then continue else return
  return xeXamShowSigninUI(user_index, users_needed, sent_flags);
}
DECLARE_XAM_EXPORT1(XamShowNuiSigninUI, kUserProfiles, kSketchy);

dword_result_t XamShowSigninUIp_entry(dword_t user_index, dword_t users_needed,
                                      dword_t flags) {
  return xeXamShowSigninUI(user_index, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUIp, kUserProfiles, kImplemented);

dword_result_t XamShowCreateProfileUIEx_entry(dword_t user_index, dword_t flag,
                                              lpstring_t unkn2_ptr) {
  return xeXamShowCreateProfileUIEx(user_index, flag, unkn2_ptr);
}
DECLARE_XAM_EXPORT1(XamShowCreateProfileUIEx, kUserProfiles, kImplemented);

dword_result_t XamShowCreateProfileUI_entry(dword_t user_index, dword_t flag) {
  return xeXamShowCreateProfileUIEx(user_index, flag, 0);
}
DECLARE_XAM_EXPORT1(XamShowCreateProfileUI, kUserProfiles, kImplemented);

dword_result_t XamShowAchievementsUI_entry(dword_t user_index,
                                           dword_t title_id) {

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_NO_SUCH_USER;
  }

  uint32_t proper_title_id =
      title_id ? title_id.value()
               : kernel_state()->xam_state()->spa_info()->title_id();

  const auto info =
      kernel_state()->xam_state()->user_tracker()->GetUserTitleInfo(
          user->xuid(), proper_title_id);

  if (!info) {
    return X_ERROR_NO_SUCH_USER;
  }

  xeXamSkipDisplayDialog("XamShowAchievementsUI");
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowAchievementsUI, kUserProfiles, kImplemented);

dword_result_t XamShowGamerCardUI_entry(dword_t user_index) {

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_ACCESS_DENIED;
  }

  xeXamSkipDisplayDialog("XamShowGamerCardUI");
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowGamerCardUI, kUserProfiles, kImplemented);

dword_result_t XamShowEditProfileUI_entry(dword_t user_index) {

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_ACCESS_DENIED;
  }

  xeXamSkipDisplayDialog("XamShowEditProfileUI");
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowEditProfileUI, kUserProfiles, kImplemented);

bool xeXamChooseIndieGame(std::string* file_name, uint32_t* device_id,
                          std::string* display_name) {
  return false;
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(UI);

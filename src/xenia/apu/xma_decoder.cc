/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xma_decoder.h"
#include <new>

#include "xenia/apu/xma_context.h"
#include "xenia/apu/xma_context_fake.h"
#include "xenia/apu/xma_context_master.h"
#include "xenia/apu/xma_context_new.h"
#include "xenia/apu/xma_context_old.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xthread.h"
extern "C" {
#include "third_party/FFmpeg/libavutil/log.h"
}  // extern "C"

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal XmaDecoder handling take it, to prevent duplicate
// implementations. They can be found in xboxkrnl_audio_xma.cc
//
// XMA details:
// https://devel.nuclex.org/external/svn/directx/trunk/include/xma2defs.h
// https://github.com/gdawg/fsbext/blob/master/src/xma_header.h
//
// XAudio2 uses XMA under the covers, and seems to map with the same
// restrictions of frame/subframe/etc:
// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.xaudio2.xaudio2_buffer(v=vs.85).aspx
//
// XMA contexts are 64b in size and tight bitfields. They are in physical
// memory not usually available to games. Games will use MmMapIoSpace to get
// the 64b pointer in user memory so they can party on it. If the game doesn't
// do this, it's likely they are either passing the context to XAudio or
// using the XMA* functions.

DEFINE_bool(ffmpeg_verbose, false, "Verbose FFmpeg output (debug and above)",
            "APU");

DEFINE_bool(use_dedicated_xma_thread, false,
            "Decode XMA on a separate thread.\n"
            "Off decodes on the guest thread that kicked the context, costing "
            "that thread the decode but leaving the title's data always "
            "current.\n"
            "On returns from the kick before the decode finishes, moving the "
            "work off the guest thread at the cost of a title that reads its "
            "context straight afterwards seeing stale data.",
            "APU");
UPDATE_from_bool(use_dedicated_xma_thread, 2026, 8, 21, 0, true);

DEFINE_string(
    xma_decoder, "new",
    "Decoder version used to process XMA audio.\n"
    "Use: [fake, master, old, new]\n"
    " fake: \n  No audio will be decoded.\n"
    " master: \n  Version of decoder exactly like on base version of Xenia.\n"
    " old: \n  Decoder based on master version of decoder with few "
    "improvements.\n"
    " new: \n  New version of decoder. Provides highest stability, but isn't "
    "yet finished.\n",
    "APU");

UPDATE_from_string(xma_decoder, 2026, 2, 16, 12, "old");

namespace xe {
namespace apu {

XmaDecoder::XmaDecoder(cpu::Processor* processor)
    : memory_(processor->memory()), processor_(processor) {
  pause_gate_.reset(xe_pause_gate_new());
  if (!pause_gate_) throw std::bad_alloc();
}

XmaDecoder::~XmaDecoder() { Shutdown(); }

void av_log_callback(void* avcl, int level, const char* fmt, va_list va) {
  if (!cvars::ffmpeg_verbose && level > AV_LOG_WARNING) {
    return;
  }

  char level_char = '?';
  LogLevel log_level;
  switch (level) {
    case AV_LOG_ERROR: {
      level_char = '!';
      log_level = xe::LogLevel::Error;
      break;
    }
    case AV_LOG_WARNING: {
      level_char = 'w';
      log_level = xe::LogLevel::Warning;
      break;
    }
    case AV_LOG_INFO: {
      level_char = 'i';
      log_level = xe::LogLevel::Info;
      break;
    }
    case AV_LOG_VERBOSE: {
      level_char = 'v';
      log_level = xe::LogLevel::Debug;
      break;
    }
    case AV_LOG_DEBUG: {
      level_char = 'd';
      log_level = xe::LogLevel::Debug;
      break;
    }
    default: {
      level_char = '?';
      log_level = xe::LogLevel::Debug;
      break;
    }
  }

  StringBuffer buff;
  buff.AppendVarargs(fmt, va);
  xe::logging::AppendLogLineFormat(LogSrc::Apu, log_level, level_char,
                                   "ffmpeg: {}", buff.to_string_view());
}

X_STATUS XmaDecoder::Setup(kernel::KernelState* kernel_state) {
  uint32_t mode;
  // Reject duplicate setup before replacing an owned context table.
  if (context_data_first_ptr_ || worker_thread_) return X_STATUS_UNSUCCESSFUL;
  // Setup ffmpeg logging callback
  av_log_set_callback(av_log_callback);

  // Let the processor know we want register access callbacks.
  memory_->AddVirtualMappedRange(
      0x7FEA0000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(MMIOReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(MMIOWriteRegisterThunk));

  // Setup XMA context data.
  // The Xbox 360 kernel allocates the contexts with X_PAGE_NOCACHE |
  // X_PAGE_READWRITE and writes MmGetPhysicalAddress for the address to the
  // register.
  context_data_first_ptr_ = memory()->SystemHeapAlloc(
      sizeof(XMA_CONTEXT_DATA) * kContextCount, 256, kSystemHeapPhysical);
  if (!context_data_first_ptr_) return X_STATUS_NO_MEMORY;
  context_data_last_ptr_ =
      context_data_first_ptr_ + (sizeof(XMA_CONTEXT_DATA) * kContextCount - 1);
  register_file_[XmaRegister::ContextArrayAddress] =
      memory()->GetPhysicalAddress(context_data_first_ptr_);

  // Freeze the actual style once for this table; mutable config must not
  // create mixed context types or later determine snapshot casts.
  mode = XE_XMA_MODE_RAW;
  if (cvars::xma_decoder == "fake") mode = XE_XMA_MODE_FAKE;
  else if (cvars::xma_decoder == "master") mode = XE_XMA_MODE_MASTER;
  else if (cvars::xma_decoder == "old") mode = XE_XMA_MODE_OLD;
  if (!InitializeContexts(mode)) {
    Shutdown();
    return X_STATUS_UNSUCCESSFUL;
  }

  retro_atomic_store_release_int(&worker_running_, 1);
  work_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  assert_not_null(work_event_);
  worker_thread_ =
      kernel::object_ref<kernel::XHostThread>(new kernel::XHostThread(
          kernel_state, 128 * 1024, 0,
          [this]() {
            if (cvars::use_dedicated_xma_thread) {
              WorkerThreadMain();
            }
            retro_atomic_store_release_int(&worker_running_, 0);
            return 0;
          },
          kernel_state
              ->GetIdleProcess()));  // this one doesnt need any process
                                     // actually. never calls any guest code
  worker_thread_->set_name("XMA Decoder");
  worker_thread_->set_can_debugger_suspend(true);
  worker_thread_->Create();

  return X_STATUS_SUCCESS;
}

bool XmaDecoder::InitializeContexts(uint32_t mode) {
  uint32_t i, guest_ptr;
  if (!context_data_first_ptr_ || mode >= XE_XMA_MODE_UNSET) return false;
  for (i = 0; i < kContextCount; ++i) {
    if (contexts_[i]) return false;
  }
  context_mode_ = mode;
  for (i = 0; i < kContextCount; ++i) {
    switch (mode) {
      case XE_XMA_MODE_FAKE: contexts_[i] = new (std::nothrow) XmaContextFake(); break;
      case XE_XMA_MODE_MASTER: contexts_[i] = new (std::nothrow) XmaContextMaster(); break;
      case XE_XMA_MODE_OLD: contexts_[i] = new (std::nothrow) XmaContextOld(); break;
      default: contexts_[i] = new (std::nothrow) XmaContextNew(); break;
    }
    guest_ptr = context_data_first_ptr_ + i * sizeof(XMA_CONTEXT_DATA);
    if (!contexts_[i] || contexts_[i]->Setup(i, memory(), guest_ptr)) return false;
  }
  register_file_[XmaRegister::NextContextIndex] = 1;
  context_bitmap_.Resize(kContextCount);
  context_bitmap_.Reset(); // Newly constructed owners all begin unallocated.
  return true;
}

bool XmaDecoder::CaptureState(xe_xma_decoder_view* out) const {
  uint32_t i;
  if (!out) return false;
  out->state.version = 0;
  if (context_mode_ != XE_XMA_MODE_RAW || !context_data_first_ptr_ ||
      !StateQuiescent() ||
      !context_bitmap_.CaptureState(out->free_slots, sizeof(out->free_slots)))
    return false;
  out->state.mode = context_mode_;
  out->state.count = kContextCount;
  out->state.context_first = context_data_first_ptr_;
  out->state.context_last = context_data_last_ptr_;
  out->registers = register_file_.values;
  out->register_bytes = sizeof(register_file_.values);
  for (i = 0; i < kContextCount; ++i) {
    if (!contexts_[i] ||
        !static_cast<const XmaContextNew*>(contexts_[i])->CaptureState(&out->owners[i]))
      return false;
  }
  out->state.version = 2;
  // Structural slot/bitmap checks only; restore must also prepare and validate
  // every saved codec payload before it mutates any live component.
  if (!xe_xma_decoder_view_valid(out, context_data_first_ptr_, context_data_last_ptr_)) {
    out->state.version = 0;
    return false;
  }
  return true;
}

bool XmaDecoder::StateQuiescent() const {
  return !retro_atomic_load_acquire_int(&worker_running_) ||
      (is_paused() && xe_pause_gate_wait_workers(pause_gate_.get(), 1, 0));
}

bool XmaDecoder::StageStorageValid(const xe_xma_decoder_stage* stage) const {
  uint32_t i;
  if (!stage || context_mode_ != XE_XMA_MODE_RAW ||
      !xe_xma_state_disjoint(stage, sizeof(*stage), this, sizeof(*this))) return false;
  for (i = 0; i < kContextCount; ++i) {
    if (!contexts_[i] || !xe_xma_state_disjoint(stage, sizeof(*stage),
          contexts_[i], sizeof(XmaContextNew))) return false;
  }
  return true;
}

bool XmaDecoder::StateRegistersValid(const uint32_t* registers) const {
  uint32_t physical, i;
  if (!registers || (uintptr_t)registers > UINTPTR_MAX - sizeof(register_file_.values))
    return false;
  if (registers != register_file_.values &&
      !xe_xma_state_disjoint(registers, sizeof(register_file_.values), this, sizeof(*this)))
    return false;
  for (i = 0; i < kContextCount; ++i) {
    if (!contexts_[i] || !xe_xma_state_disjoint(registers, sizeof(register_file_.values),
          contexts_[i], sizeof(XmaContextNew))) return false;
  }
  // Read through memcpy: external state backing need not be word-aligned.
  memcpy(&physical, (const uint8_t*)registers + XmaRegister::ContextArrayAddress * 4, 4);
  return physical == memory()->GetPhysicalAddress(context_data_first_ptr_);
}

bool XmaDecoder::PrepareState(const xe_xma_decoder_view* source,
                              xe_xma_decoder_stage* stage) const {
  uint32_t i, c;
  if (!StageStorageValid(stage) || stage->owner || stage->state.version ||
      !StateQuiescent() || !xe_xma_decoder_view_valid(source,
          context_data_first_ptr_, context_data_last_ptr_) ||
      !StateRegistersValid(source->registers) ||
      !xe_xma_state_disjoint(stage, sizeof(*stage), source, sizeof(*source)) ||
      !xe_xma_state_disjoint(stage, sizeof(*stage), source->registers,
          sizeof(register_file_.values))) return false;
  // Validate all aliases before writing the output arena. Input pointers are
  // native borrowed views, not trusted merely because metadata is well-shaped.
  for (i = 0; i < kContextCount; ++i) {
    if (stage->owners[i].candidate || stage->owners[i].owner ||
        !xe_xma_state_disjoint(stage, sizeof(*stage), source->owners[i].pcm,
            source->owners[i].pcm_bytes)) return false;
    for (c = 0; c < 2; ++c) {
      if (!xe_xma_state_disjoint(stage, sizeof(*stage), source->owners[i].codec.history[c],
            source->owners[i].codec.history_samples[c] * sizeof(float))) return false;
    }
  }
  stage->state = source->state;
  stage->owner = this;
  stage->registers = source->registers;
  memcpy(stage->free_slots, source->free_slots, sizeof(stage->free_slots));
  for (i = 0; i < kContextCount; ++i) {
    if (!static_cast<const XmaContextNew*>(contexts_[i])->PrepareState(
          &source->owners[i], &stage->owners[i])) {
      DiscardState(stage);
      return false;
    }
  }
  if (!CanCommitState(stage)) {
    DiscardState(stage);
    return false;
  }
  return true;
}

bool XmaDecoder::CanCommitState(const xe_xma_decoder_stage* stage) const {
  uint32_t i, j, free_bit;
  const XmaContextNew* context;
  uint64_t current_slots[5];
  if (!StageStorageValid(stage) || stage->owner != this || !StateQuiescent() ||
      !xe_xma_decoder_state_valid(&stage->state, context_data_first_ptr_, context_data_last_ptr_) ||
      !StateRegistersValid(stage->registers) ||
      !xe_xma_state_disjoint(stage, sizeof(*stage), stage->registers,
          sizeof(register_file_.values)) ||
      !context_bitmap_.CaptureState(current_slots, sizeof(current_slots))) return false;
  for (i = 0; i < kContextCount; ++i) {
    context = static_cast<const XmaContextNew*>(contexts_[i]);
    if (!context->CanCommitState(&stage->owners[i])) return false;
    free_bit = (uint32_t)((stage->free_slots[i / 64] >> (63 - (i & 63))) & 1);
    if (free_bit == stage->owners[i].state.allocated ||
        !xe_xma_state_disjoint(stage->owners[i].pcm, stage->owners[i].pcm_bytes,
            stage, sizeof(*stage))) return false;
    for (j = 0; j < kContextCount; ++j) {
      if (stage->owners[i].candidate == stage->owners[j].previous ||
          (i != j && stage->owners[i].candidate == stage->owners[j].candidate)) return false;
    }
    // Own-frame restore may overlap and uses memmove. Cross-owner aliases must
    // not be overwritten by an earlier commit or a released codec/frame.
    for (j = 0; stage->owners[i].pcm_bytes && j < kContextCount; ++j) {
      if (i == j && stage->owners[i].pcm_bytes &&
          (uintptr_t)stage->owners[i].pcm >= (uintptr_t)context->raw_frame_ &&
          (uintptr_t)stage->owners[i].pcm - (uintptr_t)context->raw_frame_ <=
              sizeof(context->raw_frame_) - stage->owners[i].pcm_bytes) continue;
      if (!xe_xma_state_disjoint(stage->owners[i].pcm, stage->owners[i].pcm_bytes,
            contexts_[j], sizeof(XmaContextNew))) return false;
    }
    if (!xe_xma_state_disjoint(stage->owners[i].pcm, stage->owners[i].pcm_bytes,
          this, sizeof(*this))) return false;
  }
  return true;
}

bool XmaDecoder::CommitState(xe_xma_decoder_stage* stage) {
  uint32_t i;
  if (!CanCommitState(stage)) return false;
  // This is the only remaining fallible operation; it checks before storing.
  // Exclusive ownership prevents observers seeing the intermediate map/owners.
  if (!context_bitmap_.RestoreState(stage->free_slots, sizeof(stage->free_slots))) return false;
  for (i = 0; i < kContextCount; ++i)
    static_cast<XmaContextNew*>(contexts_[i])->CommitPreparedState(&stage->owners[i]);
  if (stage->registers != register_file_.values)
    memmove(register_file_.values, stage->registers, sizeof(register_file_.values));
  memset(stage, 0, sizeof(*stage));
  return true;
}

void XmaDecoder::DiscardState(xe_xma_decoder_stage* stage) {
  uint32_t i;
  if (!stage) return;
  for (i = 0; i < kContextCount; ++i) XmaContextNew::DiscardState(&stage->owners[i]);
  memset(stage, 0, sizeof(*stage));
}

void XmaDecoder::WorkerThreadMain() {
  while (retro_atomic_load_acquire_int(&worker_running_)) {
    if (!xe_pause_gate_wait_index(pause_gate_.get(), 0)) break;
    // Okay, let's loop through XMA contexts to find ones we need to decode!
    bool did_work = false;
    for (uint32_t n = 0; n < kContextCount; n++) {
      did_work = contexts_[n]->Work() || did_work;
    }

    if (is_paused()) continue;

    if (did_work) {
      continue;
    }
    xe::threading::Wait(work_event_.get(), false);
  }
}

void XmaDecoder::Shutdown() {
  retro_atomic_store_release_int(&worker_running_, 0);
  xe_pause_gate_stop(pause_gate_.get());

  if (work_event_) {
    work_event_->Set();
  }

  if (worker_thread_) {
    // Wait for work thread.
    xe::threading::Wait(worker_thread_->thread(), false);
    worker_thread_.reset();
  }

  // Decoder objects own packet buffers and codec history. Release them only
  // after their sole worker has stopped, including partially completed setup.
  for (uint32_t i = 0; i < kContextCount; ++i) {
    delete contexts_[i];
    contexts_[i] = nullptr;
  }

  if (context_data_first_ptr_) {
    memory()->SystemHeapFree(context_data_first_ptr_);
  }

  context_data_first_ptr_ = 0;
  context_data_last_ptr_ = 0;
  context_mode_ = XE_XMA_MODE_UNSET;
}

int XmaDecoder::GetContextId(uint32_t guest_ptr) {
  static_assert_size(XMA_CONTEXT_DATA, 64);
  if (guest_ptr < context_data_first_ptr_ ||
      guest_ptr > context_data_last_ptr_) {
    return -1;
  }
  assert_zero(guest_ptr & 0x3F);
  return (guest_ptr - context_data_first_ptr_) >> 6;
}

uint32_t XmaDecoder::AllocateContext() {
  size_t index = context_bitmap_.Acquire();
  if (index == -1) {
    // Out of contexts.
    return 0;
  }

  XmaContext& context = *contexts_[index];
  assert_false(context.is_allocated());
  context.set_is_allocated(true);
  return context.guest_ptr();
}

void XmaDecoder::ReleaseContext(uint32_t guest_ptr) {
  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = *contexts_[context_id];
  assert_true(context.is_allocated());
  context.Release();
  context_bitmap_.Release(context_id);
}

bool XmaDecoder::BlockOnContext(uint32_t guest_ptr, bool poll) {
  auto context_id = GetContextId(guest_ptr);
  assert_true(context_id >= 0);

  XmaContext& context = *contexts_[context_id];
  return context.Block(poll);
}

uint32_t XmaDecoder::ReadRegister(uint32_t addr) {
  auto r = (addr & 0xFFFF) / 4;

  assert_true(r < XmaRegisterFile::kRegisterCount);

  switch (r) {
    case XmaRegister::ContextArrayAddress:
      break;
    case XmaRegister::CurrentContextIndex: {
      // 0606h (1818h) is rotating context processing # set to hardware ID of
      // context being processed.
      // If bit 200h is set, the locking code will possibly collide on hardware
      // IDs and error out, so we should never set it (I think?).
      uint32_t& current_context_index =
          register_file_[XmaRegister::CurrentContextIndex];
      uint32_t& next_context_index =
          register_file_[XmaRegister::NextContextIndex];
      // To prevent games from seeing a stuck XMA context, return a rotating
      // number.
      current_context_index = next_context_index;
      next_context_index = (next_context_index + 1) % kContextCount;
      break;
    }
    default:
      const auto register_info = register_file_.GetRegisterInfo(r);
      if (register_info) {
        XELOGW("XMA: Read from unhandled register ({:04X}, {})", r,
               register_info->name);
      } else {
        XELOGW("XMA: Read from unknown register ({:04X})", r);
      }
      break;
  }

  return xe::byte_swap(register_file_[r]);
}

void XmaDecoder::WriteRegister(uint32_t addr, uint32_t value) {
  SCOPE_profile_cpu_f("apu");

  uint32_t r = (addr & 0xFFFF) / 4;
  value = xe::byte_swap(value);

  assert_true(r < XmaRegisterFile::kRegisterCount);
  register_file_[r] = value;

  if (r >= XmaRegister::Context0Kick && r <= XmaRegister::Context9Kick) {
    // Context kick command.
    // This will kick off the given hardware contexts.
    // Basically, this kicks the SPU and says "hey, decode that audio!"
    // XMAEnableContext

    // The context ID is a bit in the range of the entire context array.
    const uint32_t base_context_id = (r - XmaRegister::Context0Kick) * 32;
    while (value) {
      const uint32_t context_id = base_context_id + std::countr_zero(value);
      auto& context = *contexts_[context_id];
      context.Enable();
      if (!cvars::use_dedicated_xma_thread) {
        context.Work();
      }
      value &= value - 1;
    }
    // Signal the decoder thread to start processing.
    work_event_->SetBoostPriority();
  } else if (r >= XmaRegister::Context0Lock && r <= XmaRegister::Context9Lock) {
    // Context lock command.
    // This requests a lock by flagging the context.
    // XMADisableContext
    const uint32_t base_context_id = (r - XmaRegister::Context0Lock) * 32;
    while (value) {
      const uint32_t context_id = base_context_id + std::countr_zero(value);
      auto& context = *contexts_[context_id];
      context.Disable();
      // Ensure the worker isn't mid-processing this context.
      context.Block(false);
      value &= value - 1;
    }
  } else if (r >= XmaRegister::Context0Clear &&
             r <= XmaRegister::Context9Clear) {
    // Context clear command.
    // This will reset the given hardware contexts.
    const uint32_t base_context_id = (r - XmaRegister::Context0Clear) * 32;
    while (value) {
      const uint32_t context_id = base_context_id + std::countr_zero(value);
      auto& context = *contexts_[context_id];
      context.Clear();
      value &= value - 1;
    }
  } else {
    // 0601h (1804h) is written to with 0x02000000 and 0x03000000 around a lock
    // operation
    switch (r) {
      case 0x601:
        break;
      default: {
        const auto register_info = register_file_.GetRegisterInfo(r);
        if (register_info) {
          XELOGW("XMA: Write to unhandled register ({:04X}, {}): {:08X}", r,
                 register_info->name, value);
        } else {
          XELOGW("XMA: Write to unknown register ({:04X}): {:08X}", r, value);
        }
        break;
      }
#pragma warning(suppress : 4065)
    }
  }
}

void XmaDecoder::Pause() {
  if (!PauseForState(2000000)) XELOGE("XmaDecoder: worker pause timed out");
}

bool XmaDecoder::PauseForState(int64_t timeout_us) {
  if (timeout_us < 0 || (worker_thread_ &&
      kernel::XThread::IsInThread(worker_thread_.get()))) return false;
  const bool was_paused=is_paused();
  xe_pause_gate_set(pause_gate_.get(), 1);
  SignalWork();
  // Without a decoder worker, the controller has already stopped synchronous
  // guest callers. No native thread exists to acknowledge this gate.
  if (!cvars::use_dedicated_xma_thread ||
      !retro_atomic_load_acquire_int(&worker_running_) ||
      xe_pause_gate_wait_workers(pause_gate_.get(), 1, timeout_us)) return true;
  if(!was_paused) Resume();
  return false;
}

void XmaDecoder::Resume() {
  xe_pause_gate_set(pause_gate_.get(), 0);
}

}  // namespace apu
}  // namespace xe

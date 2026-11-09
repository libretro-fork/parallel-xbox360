/**
 * Xenia logging bridge. BSD license; see LICENSE.
 * The frontend owns output. No worker, queue, file, console or flush policy.
 */
#include "xenia/base/logging.h"
#include <algorithm>
#include <cstdlib>
#include <retro_atomic.h>
#include "xenia/base/cvar.h"

DEFINE_uint32(log_mask, 0, "Disabled diagnostic categories", "Logging");
DEFINE_int32(log_level, 1, "Maximum frontend severity (0=error, 1=warning)", "Logging");

namespace xe {
static void (*frontend_callback)(int, const char*, size_t, void*) = nullptr;
static void* frontend_opaque = nullptr;
static retro_atomic_int_t logging_active = 0;
static retro_atomic_size_t frame_number = 0;
/* Per-thread formatting storage; sinks borrow it only for the callback.
 * A frontend logging callback may re-enter the core, so reject recursion. */
static thread_local char message[4096];
static thread_local bool emitting = false;
void SetLogCallback(void (*callback)(int, const char*, size_t, void*), void* opaque) {
  frontend_callback = callback;
  frontend_opaque = opaque;
}
void InitializeLogging(std::string_view app_name) {
  (void)app_name;
  retro_atomic_store_release_int(&logging_active, frontend_callback ? 1 : 0);
}
void ShutdownLogging() {
  /* The emulator joins its workers before this call. No deferred callbacks. */
  retro_atomic_store_release_int(&logging_active, 0);
  frontend_callback = nullptr;
  frontend_opaque = nullptr;
}
void FlushLog() {}  // Delivery is synchronous; output policy belongs to the host.
bool IsLogWriterThread() { return false; }
bool logging::IsLogEnabled(LogLevel level, uint32_t mask) {
#if !XE_ENABLE_VERBOSE_LOGGING
  if (level > LogLevel::Warning) return false;
#endif
  return level >= LogLevel::Error && !emitting &&
      retro_atomic_load_acquire_int(&logging_active) &&
      static_cast<int32_t>(level) <= cvars::log_level && !(mask & cvars::log_mask);
}
uint32_t logging::GetFrameNumber() {
  return static_cast<uint32_t>(retro_atomic_load_relaxed_size(&frame_number));
}
void logging::IncrementFrameNumber() { retro_atomic_fetch_add_size(&frame_number, 1); }
uint32_t logging::internal::GetLogLevel() { return cvars::log_level; }
std::pair<char*, size_t> logging::internal::GetThreadBuffer() {
  return {message, sizeof(message)};
}
void logging::AppendLogLine(LogLevel level, char prefix, std::string_view text,
                            uint32_t mask) {
  (void)prefix;
  if (!ShouldLog(level, mask) || text.empty()) return;
  emitting = true;
  frontend_callback(static_cast<int>(level), text.data(), text.size(), frontend_opaque);
  emitting = false;
}
void logging::internal::AppendLogLine(LogLevel level, char prefix, size_t size) {
  logging::AppendLogLine(level, prefix,
      std::string_view(message, std::min(size, sizeof(message))));
}
void FatalError(const std::string_view str) {
  logging::AppendLogLine(LogLevel::Error, 'x', str);


  ShutdownLogging();

#if XE_PLATFORM_ANDROID
  // Throw an error that can be reported to the developers via the store.
  std::abort();
#else
  // skip static destructors so they can't race with worker threads still
  // running and corrupt the heap, at_quick_exit handlers will take care
  // of necessary cleanup (e.g. /dev/shm/xenia* files on linux )
#if defined(__MINGW32__) && !defined(_UCRT)
  _exit(EXIT_FAILURE);
#else
  std::quick_exit(EXIT_FAILURE);
#endif
#endif  // XE_PLATFORM_ANDROID
}

}  // namespace xe

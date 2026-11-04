/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PROFILING_H_
#define XENIA_BASE_PROFILING_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>

#include "xenia/base/platform.h"
#include "xenia/base/string.h"

#ifndef XE_OPTION_PROFILING
#define XE_OPTION_PROFILING 0
#endif


#if XE_OPTION_PROFILING
// Pollutes the global namespace. Yuck.
// These three change the layout of the profiler state, so profiling.cc repeats
// them verbatim before it includes microprofile for the implementation. Keep
// the two lists in sync.
#define MICROPROFILE_MAX_THREADS 256
// Do not raise. One token is taken per guest function and the guest budget in
// profiling.cc is what stops FTRACE logging every guest call: past it
// GetGuestFunctionToken hands back MICROPROFILE_INVALID_TOKEN, which
// MicroProfileEnter drops on the floor.
// With more tokens the command processor thread overruns its log ring within a
// frame and microprofile's asserts, which are live in release builds, fire.
// Per function execution counts come from the coverage counters instead.
#define MICROPROFILE_MAX_TIMERS 1024
// Nothing here uses meta counters, and they cost 5 arrays per slot sized by
// the timer count.
#define MICROPROFILE_META_MAX 1
#include <microprofile/microprofile.h>
#endif  // XE_OPTION_PROFILING

namespace xe {

#if XE_OPTION_PROFILING

// Defines a profiling scope for CPU tasks.
// Use `SCOPE_profile_cpu(name)` to activate the scope.
#define DEFINE_profile_cpu(name, group_name, scope_name) \
  MICROPROFILE_DEFINE(name, group_name, scope_name,      \
                      xe::Profiler::GetColor(scope_name))

// Declares a previously defined profile scope. Use in a translation unit.
#define DECLARE_profile_cpu(name) MICROPROFILE_DECLARE(name)

// Defines a profiling scope for GPU tasks.
// Use `COUNT_profile_gpu(name)` to activate the scope.
#define DEFINE_profile_gpu(name, group_name, scope_name) \
  MICROPROFILE_DEFINE_GPU(name, group_name, scope_name,  \
                          xe::Profiler::GetColor(scope_name))

// Declares a previously defined profile scope. Use in a translation unit.
#define DECLARE_profile_gpu(name) MICROPROFILE_DECLARE_GPU(name)

// Enters a previously defined CPU profiling scope, active for the duration
// of the containing block.
#define SCOPE_profile_cpu(name) MICROPROFILE_SCOPE(name)

// Enters a CPU profiling scope, active for the duration of the containing
// block. No previous definition required.
#define SCOPE_profile_cpu_i(group_name, scope_name) \
  MICROPROFILE_SCOPEI(group_name, scope_name,       \
                      xe::Profiler::GetColor(scope_name))

// Enters a CPU profiling scope by function name, active for the duration of
// the containing block. No previous definition required.
#define SCOPE_profile_cpu_f(group_name)         \
  MICROPROFILE_SCOPEI(group_name, __FUNCTION__, \
                      xe::Profiler::GetColor(__FUNCTION__))

// Enters a previously defined GPU profiling scope, active for the duration
// of the containing block.
#define SCOPE_profile_gpu(name) MICROPROFILE_SCOPEGPU(name)

// Enters a GPU profiling scope, active for the duration of the containing
// block. No previous definition required.
#define SCOPE_profile_gpu_i(group_name, scope_name) \
  MICROPROFILE_SCOPEGPUI(group_name, scope_name,    \
                         xe::Profiler::GetColor(scope_name))

// Enters a GPU profiling scope by function name, active for the duration of
// the containing block. No previous definition required.
#define SCOPE_profile_gpu_f(group_name)            \
  MICROPROFILE_SCOPEGPUI(group_name, __FUNCTION__, \
                         xe::Profiler::GetColor(__FUNCTION__))

// Adds a number to a counter
#define COUNT_profile_add(name, count) MICROPROFILE_COUNTER_ADD(name, count)

// Subtracts a number to a counter
#define COUNT_profile_sub(name, count) MICROPROFILE_COUNTER_SUB(name, count)

// Sets a counter's value
#define COUNT_profile_set(name, count) MICROPROFILE_COUNTER_SET(name, count)

// Tracks a CPU value counter.
#define COUNT_profile_cpu(name, count) MICROPROFILE_META_CPU(name, count)

// Tracks a GPU value counter.
#define COUNT_profile_gpu(name, count) MICROPROFILE_META_GPU(name, count)

// Returns a cached CPU token naming a guest function by its address (group
// "guestfn", name = 8-hex address). Thread-safe. Used to profile guest code by
// address from the JIT dispatch and function tracers.
MicroProfileToken GetGuestFunctionToken(uint32_t guest_address);

#else

#define DEFINE_profile_cpu(name, group_name, scope_name)
#define DEFINE_profile_gpu(name, group_name, scope_name)
#define DECLARE_profile_cpu(name)
#define DECLARE_profile_gpu(name)
#define SCOPE_profile_cpu(name) \
  do {                          \
  } while (false)
#define SCOPE_profile_cpu_f(name) \
  do {                            \
  } while (false)
#define SCOPE_profile_cpu_i(group_name, scope_name) \
  do {                                              \
  } while (false)
#define SCOPE_profile_gpu(name) \
  do {                          \
  } while (false)
#define SCOPE_profile_gpu_f(name) \
  do {                            \
  } while (false)
#define SCOPE_profile_gpu_i(group_name, scope_name) \
  do {                                              \
  } while (false)
#define COUNT_profile_add(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_sub(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_set(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_cpu(name, count) \
  do {                                 \
  } while (false)
#define COUNT_profile_gpu(name, count) \
  do {                                 \
  } while (false)

#ifndef MICROPROFILE_TEXT_WIDTH
#define MICROPROFILE_TEXT_WIDTH 1
#define MICROPROFILE_TEXT_HEIGHT 1
#endif  // !MICROPROFILE_TEXT_WIDTH

#endif  // XE_OPTION_PROFILING

class Profiler {
 public:
  static bool is_enabled();
  static bool is_visible();

  // Initializes the profiler. Call at startup.
  static void Initialize();
  // Dumps data to stdout.
  static void Dump();
  // Clears the aggregate and accumulates from here on rather than tumbling
  // every N frames, so the timers cover the same window as anything else
  // gathered alongside them.
  static void ResetAggregation();

  // Appends an extra section to the CSV dump. Layers that base cannot reach
  // register their own tables this way. Writers run in registration order
  // after the profiler's own sections, and must unregister before they die.
  using DumpSectionWriter = std::function<void(FILE*)>;
  static uintptr_t RegisterDumpSection(DumpSectionWriter writer);
  static void UnregisterDumpSection(uintptr_t id);

  // Cleans up profiling, releasing all memory.
  static void Shutdown();

  // Computes a color from the given string.
  static uint32_t GetColor(const char* str);

  // Activates the calling thread for profiling.
  // This must be called immediately after launching a thread.
  static void ThreadEnter(const char* name = nullptr);
  // Deactivates the calling thread for profiling.
  static void ThreadExit();

  // Opaque profiler log. All profiling state, including the scope stack, lives
  // in one of these, and the profiler keys them by host thread. A unit of
  // execution that is not a host thread, such as a fiber multiplexed onto a
  // dispatch thread, needs its own log installed while it runs or its scopes
  // interleave with every other fiber sharing that thread.
  using ThreadLogHandle = void*;

  // Creates a log bound to no thread. Null if the profiler is out of slots, in
  // which case the caller profiles into whatever log is already current.
  static ThreadLogHandle CreateThreadLog(const char* name);
  // Makes |log| the calling thread's log, returning the one it replaced.
  static ThreadLogHandle SwapThreadLog(ThreadLogHandle log);
  // Returns |log| to the pool. Never pass a log that is currently installed on
  // another thread.
  static void RetireThreadLog(ThreadLogHandle log);

  // Starts a new frame on the profiler
  static void Flip();

};

}  // namespace xe

#endif  // XENIA_BASE_PROFILING_H_

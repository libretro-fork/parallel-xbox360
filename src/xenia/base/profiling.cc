/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// NOTE: this must be included before microprofile as macro expansion needs
// XELOGI.
#include "xenia/base/logging.h"

#include "third_party/fmt/include/fmt/printf.h"

// NOTE: microprofile must be setup first, before profiling.h is included.
#define MICROPROFILE_ENABLED 1
#define MICROPROFILE_IMPL 1
// Allocated per logging thread, a flip overrunning it loses its tail.
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (4 * 1024 * 1024)
// No GPU timers to wait on, so each flip replays as soon as it ends.
#define MICROPROFILE_GPU_FRAME_DELAY 0
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 1
#define MICROPROFILE_WEBSERVER_MAXFRAMES 3
#define MICROPROFILE_PRINTF(...)                               \
  do {                                                         \
    auto xenia_profiler_formatted = fmt::sprintf(__VA_ARGS__); \
    XELOGI("{}", xenia_profiler_formatted);                    \
  } while (false);
#define MICROPROFILE_WEBSERVER 0
#define MICROPROFILE_DEBUG 0
// These three change the layout of the profiler state, so they must match
// profiling.h exactly. This translation unit reaches microprofile first and
// would otherwise bake in the stock defaults.
#define MICROPROFILE_MAX_THREADS 256
#define MICROPROFILE_MAX_TIMERS 1024
#define MICROPROFILE_META_MAX 1
#include "third_party/microprofile/microprofile.h"

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/profiling.h"

#if XE_OPTION_PROFILING
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

DEFINE_bool(
    profiler_dump_html, false,
    "Dump the profiler capture as a browsable HTML timeline rather than "
    "aggregate CSV. Carries every logged scope, so it runs to hundreds "
    "of megabytes.",
    "UI");

namespace xe {

#if XE_OPTION_PROFILING

// Slots held back from the guest so engine scopes can still claim one. Every
// SCOPE_profile_* registers lazily on first execution, and the GPU backends
// first run theirs on the first draw, long after boot has walked enough guest
// functions to fill the table. Sized above the ~150 scope sites in src/.
static constexpr size_t kGuestFunctionTokenBudget =
    MICROPROFILE_MAX_TIMERS - 256;

MicroProfileToken GetGuestFunctionToken(uint32_t guest_address) {
  // Thread-local cache keeps the hot path lock-free. This is called per guest
  // function under FTrace, so the global lock is hit only on the first lookup
  // of an address on each thread.
  thread_local std::unordered_map<uint32_t, MicroProfileToken> local;
  auto local_it = local.find(guest_address);
  if (local_it != local.end()) {
    return local_it->second;
  }

  static std::mutex mutex;
  static std::unordered_map<uint32_t, MicroProfileToken> tokens;
  static size_t allocated = 0;
  MicroProfileToken token;
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = tokens.find(guest_address);
    if (it != tokens.end()) {
      token = it->second;
    } else if (allocated >= kGuestFunctionTokenBudget) {
      // The floor MicroProfileGetToken hits on a full table, reached earlier.
      token = MICROPROFILE_INVALID_TOKEN;
      tokens.emplace(guest_address, token);
    } else {
      char name[16];
      std::snprintf(name, sizeof(name), "%08X", guest_address);
      token = MicroProfileGetToken("guestfn", name, Profiler::GetColor(name),
                                   MicroProfileTokenTypeCpu);
      if (token != MICROPROFILE_INVALID_TOKEN) {
        ++allocated;
      }
      tokens.emplace(guest_address, token);
    }
  }
  local.emplace(guest_address, token);
  return token;
}

bool Profiler::is_enabled() { return true; }

bool Profiler::is_visible() {
  return false;
}

void Profiler::Initialize() {
  // Custom groups.
  MicroProfileSetEnableAllGroups(false);
  MicroProfileForceEnableGroup("apu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("cpu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("gpu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("internal", MicroProfileTokenTypeCpu);
  g_MicroProfile.nGroupMask = g_MicroProfile.nForceGroup;
  g_MicroProfile.nActiveGroup = g_MicroProfile.nActiveGroupWanted =
      g_MicroProfile.nGroupMask;

  // Custom timers: time, average.
  g_MicroProfile.nBars |= 0x1 | 0x2;
  g_MicroProfile.nActiveBars |= 0x1 | 0x2;

  MicroProfileSetForceEnable(true);
  MicroProfileSetEnableAllGroups(true);
  MicroProfileSetForceMetaCounters(false);
}

namespace {
std::mutex dump_sections_mutex;
std::vector<std::pair<uintptr_t, Profiler::DumpSectionWriter>> dump_sections;
uintptr_t next_dump_section_id = 1;
}  // namespace

uintptr_t Profiler::RegisterDumpSection(DumpSectionWriter writer) {
  std::lock_guard<std::mutex> lock(dump_sections_mutex);
  uintptr_t id = next_dump_section_id++;
  dump_sections.emplace_back(id, std::move(writer));
  return id;
}

void Profiler::UnregisterDumpSection(uintptr_t id) {
  std::lock_guard<std::mutex> lock(dump_sections_mutex);
  for (auto it = dump_sections.begin(); it != dump_sections.end(); ++it) {
    if (it->first == id) {
      dump_sections.erase(it);
      return;
    }
  }
}

void Profiler::ResetAggregation() {
  // Zero also asks microprofile to clear the accumulators at the next flip.
  MicroProfileSetAggregateFrames(0);
}

void Profiler::Dump() {
  const int max_frames =
      MICROPROFILE_MAX_FRAME_HISTORY - MICROPROFILE_GPU_FRAME_DELAY - 3;
  const char* path = cvars::profiler_dump_html ? "profile.html" : "profile.csv";
  FILE* f = fopen(path, "w");
  if (!f) {
    XELOGE("Failed to open {} for the profiler dump", path);
    return;
  }
  if (cvars::profiler_dump_html) {
    MicroProfileDumpHtml(MicroProfileWriteFile, f, max_frames, nullptr);
  } else {
    MicroProfileDumpCsv(MicroProfileWriteFile, f, max_frames);
    // Extra tables only make sense in the CSV, appending them to the HTML
    // would corrupt the document.
    std::lock_guard<std::mutex> lock(dump_sections_mutex);
    for (auto& section : dump_sections) {
      section.second(f);
    }
  }
  fclose(f);
  XELOGI("Profiler dump written to {}", path);
}

void Profiler::Shutdown() {
  Dump();
  MicroProfileShutdown();
}

uint32_t Profiler::GetColor(const char* str) {
  std::hash<std::string> fn;
  size_t value = fn(str);
  return value & 0xFFFFFF;
}

void Profiler::ThreadEnter(const char* name) {
  MicroProfileOnThreadCreate(name);
}

void Profiler::ThreadExit() { MicroProfileOnThreadExit(); }

Profiler::ThreadLogHandle Profiler::CreateThreadLog(const char* name) {
  MicroProfileInit();
  std::lock_guard<std::recursive_mutex> lock(MicroProfileMutex());
  return MicroProfileCreateThreadLog(name ? name : "");
}

Profiler::ThreadLogHandle Profiler::SwapThreadLog(ThreadLogHandle log) {
  MicroProfileThreadLog* previous = MicroProfileGetThreadLog();
  MicroProfileSetThreadLog(static_cast<MicroProfileThreadLog*>(log));
  return previous;
}

void Profiler::RetireThreadLog(ThreadLogHandle log) {
  if (!log) {
    return;
  }
  // MicroProfileOnThreadExit retires whatever log is current and clears it, so
  // borrow this thread's slot for the call.
  MicroProfileThreadLog* previous = MicroProfileGetThreadLog();
  MicroProfileSetThreadLog(static_cast<MicroProfileThreadLog*>(log));
  MicroProfileOnThreadExit();
  MicroProfileSetThreadLog(previous == log ? nullptr : previous);
}

void Profiler::Flip() { MicroProfileFlip(); }

#else

bool Profiler::is_enabled() { return false; }
bool Profiler::is_visible() { return false; }
void Profiler::Initialize() {}
void Profiler::Dump() {}
void Profiler::ResetAggregation() {}
uintptr_t Profiler::RegisterDumpSection(DumpSectionWriter writer) { return 0; }
void Profiler::UnregisterDumpSection(uintptr_t id) {}
void Profiler::Shutdown() {}
uint32_t Profiler::GetColor(const char* str) { return 0; }
void Profiler::ThreadEnter(const char* name) {}
void Profiler::ThreadExit() {}
Profiler::ThreadLogHandle Profiler::CreateThreadLog(const char* name) {
  return nullptr;
}
Profiler::ThreadLogHandle Profiler::SwapThreadLog(ThreadLogHandle log) {
  return nullptr;
}
void Profiler::RetireThreadLog(ThreadLogHandle log) {}
void Profiler::Flip() {}

#endif  // XE_OPTION_PROFILING

}  // namespace xe

uint32_t MicroProfileGpuInsertTimeStamp() { return 0; }

const char* MicroProfileGetThreadName() { return "TODO: get thread name!"; }

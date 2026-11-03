/* Hold the real logger in a frontend sink and interrupt its owner's join. */
#ifdef _WIN32
#define main xe_test_unused_content_close_main
#define environment xe_test_base_environment
#include "core_content_close_test.c"
#undef main
#undef environment

static HANDLE sink_entered, sink_release, apc_seen, shutdown_returned;
static HANDLE main_thread, writer_thread, workers[MAXIMUM_WAIT_OBJECTS];
static DWORD owner_id;
static unsigned worker_count;
static int coordinator_result, sink_used;

static void RETRO_CALLCONV log_sink(enum retro_log_level level,
    const char *format, ...) {
  PWSTR name = NULL;
  int is_writer;
  (void)level; (void)format;
  if (GetCurrentThreadId() == owner_id) return;
  if (FAILED(GetThreadDescription(GetCurrentThread(), &name)) || !name) return;
  is_writer = wcscmp(name, L"Logging Writer") == 0;
  LocalFree(name);
  if (!is_writer || sink_used) return;
  sink_used = 1;
  SetEvent(sink_entered);
  WaitForSingleObject(sink_release, 10000);
}
static bool RETRO_CALLCONV apc_environment(unsigned command, void *data) {
  if (command == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
    ((struct retro_log_callback *)data)->log = log_sink;
    return true;
  }
  return xe_test_base_environment(command, data);
}
static VOID CALLBACK owner_apc(ULONG_PTR data) {
  (void)data;
  SetEvent(apc_seen);
}
static int capture_workers(HMODULE module) {
  typedef LONG (NTAPI *QueryThread)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  QueryThread query;
  FARPROC symbol;
  IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
  IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((ULONG_PTR)module + dos->e_lfanew);
  ULONG_PTR base = (ULONG_PTR)module, end = base + nt->OptionalHeader.SizeOfImage;
  ULONG_PTR start;
  THREADENTRY32 entry;
  HANDLE snapshot, thread;
  PWSTR name;
  int logger;
  symbol = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
  if (!symbol) return 0;
  memcpy(&query, &symbol, sizeof(query));
  snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 0;
  memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
  if (Thread32First(snapshot, &entry)) do {
    if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
    thread = OpenThread(THREAD_QUERY_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ThreadID);
    if (!thread) continue;
    name = NULL; logger = 0; start = 0;
    if (SUCCEEDED(GetThreadDescription(thread, &name)) && name) {
      logger = wcscmp(name, L"Logging Writer") == 0;
      LocalFree(name);
    }
    if (logger) { writer_thread = thread; continue; }
    if (query(thread, 9, &start, sizeof(start), NULL) >= 0 && start >= base && start < end) {
      if (worker_count == MAXIMUM_WAIT_OBJECTS) {
        CloseHandle(thread); CloseHandle(snapshot); return 0;
      }
      workers[worker_count++] = thread;
    } else CloseHandle(thread);
  } while (Thread32Next(snapshot, &entry));
  CloseHandle(snapshot);
  return writer_thread != NULL;
}
static DWORD WINAPI coordinator(void *unused) {
  DWORD status;
  (void)unused;
  if (worker_count && WaitForMultipleObjects(worker_count, workers, TRUE, 5000) != WAIT_OBJECT_0) {
    coordinator_result = 1; SetEvent(sink_release); return 0;
  }
  if (!QueueUserAPC(owner_apc, main_thread, 0)) {
    coordinator_result = 2; SetEvent(sink_release); return 0;
  }
  status = WaitForSingleObject(apc_seen, 250);
  if (status == WAIT_OBJECT_0) {
    if (WaitForSingleObject(shutdown_returned, 5000) != WAIT_OBJECT_0)
      coordinator_result = 3;
    else if (WaitForSingleObject(writer_thread, 0) == WAIT_TIMEOUT)
      coordinator_result = 4;
  } else if (status != WAIT_TIMEOUT) coordinator_result = 5;
  /* On failure keep the writer parked until process termination, so the
   * negative control reports the lifetime error without provoking a UAF. */
  if (!coordinator_result) SetEvent(sink_release);
  return 0;
}
int main(int argc, char **argv) {
  HMODULE module;
  FARPROC symbol;
  SetEnvironment set_environment;
  Lifecycle init, deinit, unload;
  LoadGame load;
  struct retro_game_info game;
  HANDLE control;
  unsigned i;
  if (argc != 4) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  owner_id = GetCurrentThreadId(); system_dir = argv[2];
  sink_entered = CreateEventA(NULL, TRUE, FALSE, NULL);
  sink_release = CreateEventA(NULL, TRUE, FALSE, NULL);
  apc_seen = CreateEventA(NULL, TRUE, FALSE, NULL);
  shutdown_returned = CreateEventA(NULL, TRUE, FALSE, NULL);
  main_thread = OpenThread(THREAD_SET_CONTEXT | SYNCHRONIZE, FALSE, owner_id);
  if (!sink_entered || !sink_release || !apc_seen || !shutdown_returned || !main_thread) return 3;
  module = LoadLibraryA(argv[1]); if (!module) return 4;
  FIND(set_environment, "retro_set_environment");
  FIND(init, "retro_init"); FIND(deinit, "retro_deinit");
  FIND(load, "retro_load_game"); FIND(unload, "retro_unload_game");
  set_environment(apc_environment); init();
  memset(&game, 0, sizeof(game)); game.path = argv[3];
  if (load(&game)) return 5;
  if (WaitForSingleObject(sink_entered, 5000) != WAIT_OBJECT_0 || !capture_workers(module)) return 6;
  control = CreateThread(NULL, 0, coordinator, NULL, 0, NULL);
  if (!control) return 7;
  unload(); SetEvent(shutdown_returned);
  if (WaitForSingleObject(control, 6000) != WAIT_OBJECT_0) return 8;
  if (coordinator_result) {
    fprintf(stderr, "FAIL: logger join interrupted/lifetime error %d\n", coordinator_result);
    return 9;
  }
  if (WaitForSingleObjectEx(apc_seen, 1000, TRUE) != WAIT_IO_COMPLETION ||
      WaitForSingleObject(apc_seen, 0) != WAIT_OBJECT_0) return 10;
  if (WaitForSingleObject(writer_thread, 0) != WAIT_OBJECT_0 || core_threads(module)) return 11;
  deinit();
  for (i = 0; i < worker_count; ++i) CloseHandle(workers[i]);
  CloseHandle(control); CloseHandle(writer_thread); CloseHandle(main_thread);
  CloseHandle(sink_entered); CloseHandle(sink_release);
  CloseHandle(apc_seen); CloseHandle(shutdown_returned);
  if (!FreeLibrary(module)) return 12;
  puts("PASS: logger join ignores APC until writer exits and queue can be freed");
  return 0;
}
#else
#include <stdio.h>
int main(void) { puts("SKIP: Windows APC regression"); return 0; }
#endif

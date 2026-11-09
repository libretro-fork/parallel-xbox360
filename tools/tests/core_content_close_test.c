/* Failed-content teardown must leave no logger or core worker. MSVC C89, Windows. */
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <libretro.h>
typedef void (RETRO_CALLCONV *SetEnvironment)(retro_environment_t);
typedef void (RETRO_CALLCONV *Lifecycle)(void);
typedef bool (RETRO_CALLCONV *LoadGame)(const struct retro_game_info *);
static const char *system_dir;
static bool RETRO_CALLCONV environment(unsigned command, void *data) {
  if (command == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY ||
      command == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY) {
    *(const char **)data = system_dir;
    return true;
  }
  if (command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return true;
  if (command == RETRO_ENVIRONMENT_GET_VARIABLE) {
    struct retro_variable *variable = (struct retro_variable *)data;
    variable->value = NULL;
  }
  return false;
}
static unsigned logger_threads(void) {
  HANDLE snapshot, thread;
  THREADENTRY32 entry;
  PWSTR name;
  unsigned count = 0;
  snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 999;
  memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
  if (Thread32First(snapshot, &entry)) do {
    if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
    thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (!thread) continue;
    name = NULL;
    if (SUCCEEDED(GetThreadDescription(thread, &name)) && name) {
      if (!wcscmp(name, L"Logging Writer")) ++count;
      LocalFree(name);
    }
    CloseHandle(thread);
  } while (Thread32Next(snapshot, &entry));
  CloseHandle(snapshot);
  return count;
}
/* Check every thread started in the DLL, including statically linked runtime
 * wrappers, rather than relying only on the logger's descriptive name. */
static unsigned core_threads(HMODULE module) {
  typedef LONG (NTAPI *QueryThread)(HANDLE, ULONG, PVOID, ULONG, PULONG);
  QueryThread query;
  FARPROC symbol;
  IMAGE_DOS_HEADER *dos;
  IMAGE_NT_HEADERS *nt;
  ULONG_PTR base, end, start;
  HANDLE snapshot, thread;
  THREADENTRY32 entry;
  unsigned count = 0;
  symbol = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
  if (!symbol) return 999;
  memcpy(&query, &symbol, sizeof(query));
  base = (ULONG_PTR)module;
  dos = (IMAGE_DOS_HEADER *)module;
  nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
  end = base + nt->OptionalHeader.SizeOfImage;
  snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return 999;
  memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
  if (Thread32First(snapshot, &entry)) do {
    if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
    thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
    if (!thread) continue;
    start = 0;
    /* ThreadQuerySetWin32StartAddress = 9. */
    if (query(thread, 9, &start, sizeof(start), NULL) >= 0 &&
        start >= base && start < end) ++count;
    CloseHandle(thread);
  } while (Thread32Next(snapshot, &entry));
  CloseHandle(snapshot);
  return count;
}
int main(int argc, char **argv) {
  HMODULE module, still_loaded;
  FARPROC symbol;
  SetEnvironment set_environment;
  Lifecycle init, deinit, unload;
  LoadGame load;
  struct retro_game_info game;
  unsigned cycle, count;
  if (argc != 4) return 2;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  system_dir = argv[2];
  module = LoadLibraryA(argv[1]); if (!module) return 3;
#define FIND(function, export_name) do { symbol = GetProcAddress(module, export_name); if (!symbol) return 4; memcpy(&function, &symbol, sizeof(function)); } while (0)
  FIND(set_environment, "retro_set_environment");
  FIND(init, "retro_init"); FIND(deinit, "retro_deinit");
  FIND(load, "retro_load_game"); FIND(unload, "retro_unload_game");
  set_environment(environment);
  for (cycle = 0; cycle < 3; ++cycle) {
    init();
    memset(&game, 0, sizeof(game)); game.path = argv[3];
    if (load(&game)) { fputs("Invalid content unexpectedly loaded\n", stderr); return 5; }
    count = logger_threads();
    if (count != 0) { fprintf(stderr, "Unexpected logger before close, got %u\n", count); return 6; }
    unload();
    count = logger_threads();
    if (count) { fprintf(stderr, "Logger survived content close: %u thread(s)\n", count); return 7; }
    /* Repeated close/deinit must be safe. Next cycle must not create a logger. */
    unload(); deinit(); deinit();
    count = core_threads(module);
    if (count) { fprintf(stderr, "Core-owned thread entry points survived deinit: %u\n", count); return 9; }
  }
  if (!FreeLibrary(module)) return 8;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (LPCSTR)module, &still_loaded)) {
    fputs("Core remained loaded after FreeLibrary\n", stderr); return 10;
  }
  Sleep(250);
  puts("PASS: three real failed-content opens/closes, no logging worker, repeated teardown and DLL unload");
  return 0;
}

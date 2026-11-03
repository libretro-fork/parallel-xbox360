/* Isolated production-reader process benchmark; MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef XE_ZAR_COPY_FLOOR
#ifdef _WIN32
__declspec(dllexport)
#endif
/* Build separately as a shared object: keep every byte of the copy observable
 * across the same opaque function-call boundary as the production reader. */
uint64_t archive_test_copy(void *source, uint32_t node, uint64_t offset,
    size_t size, void *output) {
  (void)node;
  memcpy(output, (const unsigned char *)source + (size_t)offset, size);
  return size;
}
#else
#include <rthreads/rthreads.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
typedef FARPROC benchmark_symbol;
static void *module_open(const char *path) { return LoadLibraryA(path); }
static benchmark_symbol module_symbol(void *module, const char *name) {
  return GetProcAddress((HMODULE)module,name);
}
static void module_close(void *module) { FreeLibrary((HMODULE)module); }
#else
#include <time.h>
#include <sys/resource.h>
#include <dlfcn.h>
typedef void *benchmark_symbol;
static void *module_open(const char *path) { return dlopen(path,RTLD_NOW | RTLD_LOCAL); }
static benchmark_symbol module_symbol(void *module, const char *name) { return dlsym(module,name); }
static void module_close(void *module) { dlclose(module); }
#endif
typedef void *(*open_fn)(const char *);
typedef void (*close_fn)(void *);
typedef uint64_t (*read_fn)(void *,const char *,uint64_t,size_t,void *);
typedef uint64_t (*size_fn)(void *,const char *);
typedef uint32_t (*lookup_fn)(void *,const char *);
typedef uint64_t (*node_read_fn)(void *,uint32_t,uint64_t,size_t,void *);
typedef struct benchmark_worker {
  slock_t *lock;
  scond_t *ready;
  int stop;
} benchmark_worker;
static void idle_worker(void *opaque) {
  benchmark_worker *worker = (benchmark_worker *)opaque;
  slock_lock(worker->lock);
  while (!worker->stop) scond_wait(worker->ready,worker->lock);
  slock_unlock(worker->lock);
}
static double now(void) {
#ifdef _WIN32
  LARGE_INTEGER value, frequency;
  QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
  return (double)value.QuadPart / (double)frequency.QuadPart;
#else
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1000000000.0;
#endif
}
static unsigned long peak_kib(void) {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS info;
  memset(&info,0,sizeof(info)); info.cb = sizeof(info);
  if (!GetProcessMemoryInfo(GetCurrentProcess(),&info,sizeof(info))) return 0;
  return (unsigned long)(info.PeakWorkingSetSize / 1024);
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF,&usage)) return 0;
#ifdef __APPLE__
  return (unsigned long)usage.ru_maxrss / 1024;
#else
  return (unsigned long)usage.ru_maxrss;
#endif
#endif
}
int main(int argc, char **argv) {
  void *module = NULL, *reader = NULL, *floor_module = NULL, *source = NULL;
  benchmark_symbol symbol;
  open_fn open_reader;
  close_fn close_reader = NULL;
  read_fn read_reader;
  node_read_fn copy_reader = NULL, read_node;
  lookup_fn lookup;
  size_fn size_reader;
  unsigned char data[256], expected;
  unsigned long i, count, checksum = 0;
  uint64_t blocks, block, offset;
  double start, elapsed;
  int raw, cold, miss, floor, handle, code = 0;
  uint32_t node;
  benchmark_worker worker;
  sthread_t *thread = NULL;
  if (argc != 5 && argc != 6) return 1;
  count = strtoul(argv[4], NULL, 10);
  raw = strstr(argv[3], "raw") != NULL;
  cold = strstr(argv[3], "open") != NULL;
  miss = strstr(argv[3], "miss") != NULL;
  floor = strstr(argv[3], "floor") != NULL;
  handle = strstr(argv[3], "handle") != NULL;
  if (floor && argc != 6) return 1;
  /* Keep libc in the multithreaded mode used by the core. Otherwise glibc's
   * single-thread mutex fast path understates the real lock cost. No polling. */
  worker.lock = slock_new(); worker.ready = scond_new(); worker.stop = 0;
  if (!worker.lock || !worker.ready) { code = 13; goto done; }
  thread = sthread_create(idle_worker,&worker); if (!thread) { code = 14; goto done; }
  module = module_open(argv[1]); if (!module) { code = 2; goto done; }
#define LOAD(target, name) do { symbol = module_symbol(module, name); if (!symbol || sizeof(target) != sizeof(symbol)) { code = 3; goto done; } memcpy(&target, &symbol, sizeof(target)); } while (0)
  LOAD(open_reader,"archive_test_open"); LOAD(close_reader,"archive_test_close");
  LOAD(read_reader,"archive_test_read"); LOAD(size_reader,"archive_test_size");
  LOAD(lookup,"archive_test_lookup"); LOAD(read_node,"archive_test_read_node");
  reader = open_reader(argv[2]); if (!reader) { code = 4; goto done; }
  node = lookup(reader,"raw"); if (node == (uint32_t)-1) { code = 15; goto done; }
  blocks = size_reader(reader,"raw") / 65536;
  if (!blocks) { code = 5; goto done; }
  if (floor) {
    if (blocks > 32) blocks = 32;
    source = malloc((size_t)blocks * 65536);
    if (!source || read_reader(reader,"raw",0,(size_t)blocks * 65536,source) != blocks * 65536) { code = 10; goto done; }
    close_reader(reader); reader = NULL;
    floor_module = module_open(argv[5]);
    if (!floor_module) { code = 11; goto done; }
    symbol = module_symbol(floor_module,"archive_test_copy");
    if (!symbol || sizeof(copy_reader) != sizeof(symbol)) { code = 12; goto done; }
    memcpy(&copy_reader,&symbol,sizeof(copy_reader));
  } else if (!cold) {
    for (i = 0; i < 32 && i < blocks; ++i)
      if (read_reader(reader,"raw",(uint64_t)i * 65536,256,data) != 256) { code = 6; goto done; }
  } else { close_reader(reader); reader = NULL; }
  start = now();
  for (i = 0; i < count; ++i) {
    if (cold) { reader = open_reader(argv[2]); if (!reader) { code = 7; goto done; } }
    block = cold ? 0 : (miss ? ((uint64_t)i * 73) % blocks : i % (blocks < 32 ? blocks : 32));
    offset = block * 65536 + 19;
    if ((floor ? copy_reader(source,0,offset,256,data) :
        (handle ? read_node(reader,node,offset,256,data) :
        read_reader(reader,"raw",offset,256,data))) != 256) { code = 8; goto done; }
    expected = raw ? (unsigned char)(offset * 37) : (unsigned char)(block * 37 + 5);
    if (data[0] != expected || data[255] != (raw ? (unsigned char)((offset + 255) * 37) : expected)) { code = 9; goto done; }
    checksum += data[0] + data[255];
    if (cold) { close_reader(reader); reader = NULL; }
  }
  elapsed = now() - start;

  printf("%s,%lu,%.9f,%lu,%lu\n",argv[3],count,elapsed,peak_kib(),checksum);
done:
  if (reader && close_reader) close_reader(reader);
  if (module) module_close(module);
  if (floor_module) module_close(floor_module);
  free(source);
  if (thread) {
    slock_lock(worker.lock); worker.stop = 1; scond_signal(worker.ready);
    slock_unlock(worker.lock); sthread_join(thread);
  }
  if (worker.ready) scond_free(worker.ready);
  if (worker.lock) slock_free(worker.lock);
  return code;
}
#endif

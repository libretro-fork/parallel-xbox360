// Offline CPU benchmark only. Legacy vector and timing are not core code.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include <vulkan/vulkan.h>
static size_t page_allocations, page_bytes;
static void* page_alloc(size_t n) { void* p = std::malloc(n); if (p) { ++page_allocations; page_bytes += n; } return p; }
#define XE_COMMAND_STREAM_ALLOC page_alloc
#define XE_COMMAND_STREAM_FREE std::free
#include "core_command_stream.h"
#if defined(_MSC_VER)
#define BOUNDARY __declspec(noinline)
#else
#define BOUNDARY __attribute__((noinline, noipa))
#endif
struct Header { uint32_t command, elements; };
struct Args { VkBuffer src, dst; uint32_t count; };
static size_t args_bytes() { return (sizeof(Args) + 7) & ~size_t(7); }
static size_t packet_size(uint32_t count) { return sizeof(Header) + args_bytes() + size_t(count) * sizeof(VkBufferCopy); }
static BOUNDARY void fill(void* packet, uint32_t id, uint32_t count) {
  Header* h = static_cast<Header*>(packet);
  h->command = 1; h->elements = uint32_t((packet_size(count) - sizeof(*h)) / 8);
  Args* a = reinterpret_cast<Args*>(h + 1);
  a->src = (VkBuffer)(uintptr_t)(0x1000 + id); a->dst = (VkBuffer)(uintptr_t)(0x2000 + id); a->count = count;
  VkBufferCopy* r = reinterpret_cast<VkBufferCopy*>(reinterpret_cast<unsigned char*>(a) + args_bytes());
  for (uint32_t i = 0; i < count; ++i) { r[i].srcOffset = id + i; r[i].dstOffset = id * 2u + i; r[i].size = i + 1u; }
}
static BOUNDARY uint64_t consume(const unsigned char* data, size_t bytes) {
  uint64_t sum = 0;
  while (bytes) {
    const Header* h = reinterpret_cast<const Header*>(data);
    const Args* a = reinterpret_cast<const Args*>(h + 1);
    const VkBufferCopy* r = reinterpret_cast<const VkBufferCopy*>(reinterpret_cast<const unsigned char*>(a) + args_bytes());
    sum += h->command + h->elements + (uintptr_t)a->src + (uintptr_t)a->dst + a->count;
    for (uint32_t i = 0; i < a->count; ++i) sum += r[i].srcOffset + r[i].dstOffset + r[i].size;
    size_t size = sizeof(*h) + size_t(h->elements) * 8;
    if (size > bytes) std::abort();
    data += size; bytes -= size;
  }
  return sum;
}
struct Old {
  std::vector<uintmax_t> data;
  size_t allocations = 1, relocated = 0;
  Old() { data.reserve(1024 * 1024 / 8); }
  void reset() { data.clear(); }
  void emit(uint32_t id, uint32_t count) {
    size_t at = data.size(); size_t words = packet_size(count) / 8;
    size_t capacity = data.capacity();
    data.resize(at + words);
    if (data.capacity() != capacity) { ++allocations; relocated += at * 8; }
    fill(data.data() + at, id, count);
  }
  uint64_t replay() { return consume(reinterpret_cast<const unsigned char*>(data.data()), data.size() * 8); }
};
struct Pages {
  xe_command_stream_owner data;
  Pages() { if (!xe_command_stream_init(&data, 1024 * 1024)) throw std::bad_alloc(); }
  void reset() { xe_command_stream_reset(&data); }
  void emit(uint32_t id, uint32_t count) { void* p = xe_command_stream_append(&data, packet_size(count)); if (!p) throw std::bad_alloc(); fill(p, id, count); }
  uint64_t replay() { uint64_t sum = 0; for (const xe_command_page* p = data.first; p; p = p->next) if (p->used) sum += consume(xe_command_page_const_data(p), p->used); return sum; }
};
static volatile uint64_t sink;
template<class T> static double measure(T& storage, uint32_t packets, uint32_t count, unsigned repeats, uint64_t expected) {
  auto begin = std::chrono::steady_clock::now();
  for (unsigned repeat = 0; repeat < repeats; ++repeat) {
    storage.reset();
    for (uint32_t id = 0; id < packets; ++id) storage.emit(id, count);
    uint64_t got = storage.replay(); if (got != expected) std::abort(); sink = got;
  }
  return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() / repeats;
}
static void run(const char* name, uint32_t packets, uint32_t count, unsigned repeats) {
  page_allocations = page_bytes = 0;
  Old old; Pages pages;
  for (uint32_t id = 0; id < packets; ++id) { old.emit(id, count); pages.emit(id, count); }
  uint64_t expected = old.replay(); if (pages.replay() != expected) std::abort();
  size_t initial_page_allocations = page_allocations;
  size_t initial_old_allocations = old.allocations;
  double a[7], b[7];
  for (unsigned sample = 0; sample < 7; ++sample) {
    if (sample & 1) { b[sample] = measure(pages, packets, count, repeats, expected); a[sample] = measure(old, packets, count, repeats, expected); }
    else { a[sample] = measure(old, packets, count, repeats, expected); b[sample] = measure(pages, packets, count, repeats, expected); }
  }
  if (initial_page_allocations != page_allocations || initial_old_allocations != old.allocations) std::abort();
  std::sort(a, a + 7); std::sort(b, b + 7);
  std::printf("%s: %u packets x %u copies; vector %.2f us, pages %.2f us (%.2fx); cold allocations %zu/%zu, retained bytes %zu/%zu, growth-relocated bytes %zu/0; warm allocations 0/0\n", name, packets, count, a[3], b[3], a[3]/b[3], old.allocations, page_allocations, old.data.capacity()*8, page_bytes, old.relocated);
}
int main() {
#if defined(_MSC_VER)
  std::printf("MSVC %d x64 /O2, ", _MSC_VER);
#else
  std::printf("GCC %s x64 -O2, ", __VERSION__);
#endif
  std::puts("7 interleaved samples, median record+CPU-replay us; 1 MiB initial payload; native VkBufferCopy; no Vulkan/device/gameplay calls");
  run("small-warm", 8192, 1, 100);
  run("many-small-growth", 32768, 1, 30);
  run("medium-growth", 8192, 16, 30);
  run("oversized", 1, 65536, 100);
  return 0;
}

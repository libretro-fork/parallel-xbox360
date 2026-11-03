/* Selected libretro-common primitive integration tests; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <retro_atomic.h>
#include <retro_spsc.h>
#include <queues/mpsc_stack.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <encodings/xxh3.h>
#include <encodings/xxh64.h>
#include <encodings/crc32.h>
#include <encodings/deflate.h>
#define COUNT 100000u
static retro_spsc_t fifo;
static retro_eventcount_t changed;
static retro_atomic_int_t failed;
static retro_atomic_int_t hash_start;
static unsigned char hash_input[8192];
static void hash_worker(void *userdata) {
  unsigned i;
  xxh3_state_t state;
  (void)userdata;
  while (!retro_atomic_load_acquire_int(&hash_start)) sthread_yield();
  for (i = 0; i < 100; ++i) {
    xxh3_reset(&state, 0);
    xxh3_update(&state, hash_input, 4096);
    xxh3_update(&state, hash_input + 4096, 4096);
    if (xxh3_64(hash_input, sizeof(hash_input), 0) != UINT64_C(0xc498c4d772c8623e) ||
        xxh3_digest(&state) != UINT64_C(0xc498c4d772c8623e))
      retro_atomic_store_release_int(&failed, 1);
  }
}
static int concurrent_hash_test(void) {
  sthread_t *threads[8];
  unsigned i, count = 0;
  for (i = 0; i < sizeof(hash_input); ++i)
    hash_input[i] = (unsigned char)(i * 13 + i / 37);
  for (i = 0; i < 8; ++i) {
    threads[i] = sthread_create(hash_worker, NULL);
    if (!threads[i]) break;
    ++count;
  }
  retro_atomic_store_release_int(&hash_start, 1);
  for (i = 0; i < count; ++i) sthread_join(threads[i]);
  return count == 8 && !retro_atomic_load_acquire_int(&failed);
}
static void producer(void *userdata) {
  uint32_t i;
  int key;
  (void)userdata;
  for (i = 0; i < COUNT; ++i) {
    while (retro_spsc_write_avail(&fifo) < sizeof(i)) {
      if (retro_atomic_load_acquire_int(&failed)) return;
      key = retro_eventcount_prepare_wait(&changed);
      if (retro_spsc_write_avail(&fifo) >= sizeof(i)) {
        retro_eventcount_cancel_wait(&changed); break;
      }
      retro_eventcount_commit_wait(&changed, key);
    }
    retro_spsc_write(&fifo, &i, sizeof(i));
    retro_eventcount_notify(&changed);
  }
}
static int queue_test(void) {
  sthread_t *thread;
  uint32_t i, value;
  int key;
  retro_atomic_store_relaxed_int(&failed, 0);
  if (!retro_spsc_init(&fifo, 256) || !retro_eventcount_init(&changed)) return 0;
  thread = sthread_create(producer, NULL); if (!thread) return 0;
  for (i = 0; i < COUNT; ++i) {
    while (retro_spsc_read_avail(&fifo) < sizeof(value)) {
      key = retro_eventcount_prepare_wait(&changed);
      if (retro_spsc_read_avail(&fifo) >= sizeof(value)) {
        retro_eventcount_cancel_wait(&changed); break;
      }
      if (!retro_eventcount_commit_wait_timeout(&changed, key, 5000000)) {
        retro_atomic_store_release_int(&failed, 1);
        retro_eventcount_notify(&changed); break;
      }
    }
    if (retro_atomic_load_acquire_int(&failed)) break;
    retro_spsc_read(&fifo, &value, sizeof(value));
    retro_eventcount_notify(&changed);
    if (value != i) { retro_atomic_store_release_int(&failed, 1); break; }
  }
  retro_eventcount_notify(&changed); sthread_join(thread);
  retro_eventcount_free(&changed); retro_spsc_free(&fifo);
  return !retro_atomic_load_acquire_int(&failed);
}
struct node { mpsc_stack_node_t link; unsigned value; };
static mpsc_stack_t stack;
static struct node nodes[2048];
static void push_nodes(void *userdata) {
  unsigned i, begin = *(unsigned *)userdata;
  for (i = begin; i < begin + 1024; ++i) {
    nodes[i].value = i;
    mpsc_stack_push(&stack, &nodes[i].link);
  }
}
static int mpsc_test(void) {
  unsigned begin[2] = {0, 1024};
  unsigned char seen[2048];
  unsigned i, count = 0;
  sthread_t *threads[2];
  mpsc_stack_node_t *item, *next;
  struct node *node;
  memset(seen, 0, sizeof(seen)); mpsc_stack_init(&stack);
  threads[0] = sthread_create(push_nodes, &begin[0]);
  threads[1] = sthread_create(push_nodes, &begin[1]);
  if (!threads[0] || !threads[1]) return 0;
  sthread_join(threads[0]); sthread_join(threads[1]);
  for (item = mpsc_stack_reverse(mpsc_stack_drain(&stack)); item; item = next) {
    next = item->next; node = (struct node *)item;
    if (node->value >= 2048 || seen[node->value]) return 0;
    seen[node->value] = 1; ++count;
  }
  for (i = 0; i < 2048; ++i) if (!seen[i]) return 0;
  return count == 2048 && mpsc_stack_empty(&stack);
}
static int codec_test(void) {
  uint8_t input[8192], packed[16384], output[8192];
  void *stream;
  size_t i, read, wrote, packed_size;
  int result;
  for (i = 0; i < sizeof(input); ++i) input[i] = (uint8_t)(i * 13 + i / 37);
  stream = rdeflate_new(6, 15); if (!stream) return 0;
  rdeflate_set_in(stream, input, sizeof(input));
  rdeflate_set_out(stream, packed, sizeof(packed)); rdeflate_finish(stream);
  result = rdeflate_process(stream, &read, &wrote); packed_size = wrote;
  rdeflate_free(stream);
  if (result != RDEFLATE_PROCESS_END || read != sizeof(input)) return 0;
  stream = rinflate_new(15); if (!stream) return 0;
  rinflate_set_in(stream, packed, packed_size); rinflate_set_out(stream, output, sizeof(output));
  result = rinflate_process(stream, &read, &wrote); rinflate_free(stream);
  return result == RDEFLATE_PROCESS_END && wrote == sizeof(input) && !memcmp(input, output, sizeof(input));
}
int main(void) {
  /* First XXH3 call happens concurrently, exercising dispatch publication. */
  if (!concurrent_hash_test()) return 5;
  if (xxh3_64("", 0, 0) != UINT64_C(0x2d06800538d394c2) ||
      xxh64("", 0, 0) != UINT64_C(0xef46db3751d8e999) ||
      encoding_crc32(0, (const uint8_t *)"123456789", 9) != UINT32_C(0xcbf43926)) return 1;
  if (!queue_test()) return 2;
  if (!mpsc_test()) return 3;
  if (!codec_test()) return 4;
  puts("PASS: common hash vectors, 100000 SPSC/eventcount handoffs, MPSC nodes and DEFLATE roundtrip");
  return 0;
}

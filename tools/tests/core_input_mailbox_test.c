#include "core_input_mailbox.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %u\n",(unsigned)__LINE__); abort(); } } while (0)
#define READERS 6
#define UPDATES 50000
static xe_input_mailbox *input;
static retro_atomic_int_t stop, ready, failed, hold, entered, resume_reader;
static retro_atomic_int_t seen[UPDATES + 1][2];
static unsigned roles[READERS];
void xe_input_mailbox_test_pin(void) {
   if (retro_atomic_exchange_int(&hold, 0)) {
      retro_atomic_store_release_int(&entered, 1);
      while (!retro_atomic_load_acquire_int(&resume_reader)) {}
   }
}
static void packet_make(xe_input_packet *packet, unsigned n) {
   unsigned value = n % 30000;
   memset(packet, 0, sizeof(*packet));
   packet->packet = n; packet->buttons = (uint16_t)value;
   packet->analog.left_x = (int16_t)value;
   packet->analog.left_y = (int16_t)-(int)value;
   packet->analog.right_x = (int16_t)(value / 2);
   packet->analog.right_y = (int16_t)-(int)(value / 2);
   packet->analog.left_trigger = (uint8_t)value;
   packet->analog.right_trigger = (uint8_t)~value;
   packet->connected = 1;
}
static int packet_valid(const xe_input_packet *packet) {
   xe_input_packet expected;
   packet_make(&expected, packet->packet);
   return packet->buttons == expected.buttons &&
      packet->analog.left_x == expected.analog.left_x && packet->analog.left_y == expected.analog.left_y &&
      packet->analog.right_x == expected.analog.right_x && packet->analog.right_y == expected.analog.right_y &&
      packet->analog.left_trigger == expected.analog.left_trigger &&
      packet->analog.right_trigger == expected.analog.right_trigger && packet->connected;
}
static void pinned_reader(void *opaque) {
   xe_input_packet packet;
   (void)opaque;
   CHECK(xe_input_packet_read(input, 0, &packet) && packet.packet == 1 && packet_valid(&packet));
}
static void reader(void *opaque) {
   unsigned port = *(unsigned *)opaque % 4, previous = 0;
   xe_input_packet packet;
   xe_input_key key;
   uint16_t motor;
   retro_atomic_fetch_add_int(&ready, 1);
   while (!retro_atomic_load_acquire_int(&stop)) {
      CHECK(xe_input_packet_read(input, port, &packet));
      if (!packet_valid(&packet) || packet.packet < previous)
         retro_atomic_store_release_int(&failed, 1);
      previous = packet.packet;
      motor = (uint16_t)packet.packet;
      xe_input_rumble_post(input, port, motor, (uint16_t)~motor);
      if (xe_input_key_take(input, 0, &key)) {
         if (!key.key || key.key > UPDATES || (key.flags != 1 && key.flags != 2) ||
               retro_atomic_fetch_add_int(&seen[key.key][key.flags - 1], 1))
            retro_atomic_store_release_int(&failed, 1);
      }
   }
}
static void deterministic(void) {
   xe_input_key event, oracle[199];
   uint16_t key = 0x5800, strong, weak;
   unsigned i, count = 0;
   input = xe_input_mailbox_new(); CHECK(input);
   xe_input_begin_run(input, 60);
   xe_input_keys_update(input, 0, &key, 1, 1);
   CHECK(xe_input_key_take(input, 0, &event) && event.key == key && event.flags == 1);
   for (i = 0; i < 23; ++i) {
      xe_input_begin_run(input, 60); xe_input_keys_update(input, 0, &key, 1, 1);
      CHECK(!xe_input_key_take(input, 0, &event));
   }
   xe_input_begin_run(input, 60); xe_input_keys_update(input, 0, &key, 1, 1);
   CHECK(xe_input_key_take(input, 0, &event) && event.key == key && event.flags == 5);
   for (i = 0; i < 5; ++i) {
      xe_input_begin_run(input, 60); xe_input_keys_update(input, 0, &key, 1, 1);
      CHECK(!xe_input_key_take(input, 0, &event));
   }
   xe_input_begin_run(input, 60); xe_input_keys_update(input, 0, &key, 1, 1);
   CHECK(xe_input_key_take(input, 0, &event) && event.flags == 5);
   xe_input_keys_update(input, 0, NULL, 0, 0);
   for (i = 1; i <= 100; ++i) {
      if (i != 1) { oracle[count].key = (uint16_t)(i-1); oracle[count++].flags = 2; }
      oracle[count].key = (uint16_t)i; oracle[count++].flags = 1;
      key = (uint16_t)i; xe_input_keys_update(input, 0, &key, 1, 1);
   }
   for (i = count - 64; i < count; ++i)
      CHECK(xe_input_key_take(input, 0, &event) && event.key == oracle[i].key && event.flags == oracle[i].flags);
   CHECK(!xe_input_key_take(input, 0, &event));
   key = 101; xe_input_keys_update(input, 0, &key, 1, 1);
   xe_input_keys_update(input, 0, NULL, 0, 0);
   CHECK(!xe_input_key_take(input, 0, &event));
   xe_input_rumble_post(input, 0, 12, 34); xe_input_rumble_post(input, 0, 56, 78);
   CHECK(xe_input_rumble_take(input, 0, &strong, &weak) && strong == 56 && weak == 78);
   CHECK(!xe_input_rumble_take(input, 0, &strong, &weak));
   xe_input_mailbox_free(input);
}
static void snapshot_limits(void) {
   xe_input_lease leases[16];
   const xe_input_packet *views[16];
   xe_input_packet packet, current;
   unsigned i;
   input = xe_input_mailbox_new(); CHECK(input);
   for (i = 0; i < 16; ++i) {
      packet_make(&packet, i + 1);
      CHECK(xe_input_packet_publish(input, 0, &packet));
      views[i] = xe_input_packet_acquire(input, 0, &leases[i]);
      CHECK(views[i] && views[i]->packet == i + 1);
   }
   packet_make(&packet, 999);
   CHECK(!xe_input_packet_publish(input, 0, &packet));
   CHECK(xe_input_packet_read(input, 0, &current) && current.packet == 16);
   xe_input_packet_release(&leases[0]);
   xe_input_packet_release(&leases[0]);
   CHECK(xe_input_packet_publish(input, 0, &packet));
   for (i = 1; i < 16; ++i) {
      CHECK(views[i]->packet == i + 1 && packet_valid(views[i]));
      xe_input_packet_release(&leases[i]);
   }
   xe_input_packet_release(NULL);
   CHECK(!xe_input_packet_acquire(input, 0, NULL));
   xe_input_mailbox_free(input);
}
int main(void) {
   sthread_t *threads[READERS], *pinned;
   xe_input_packet packet, guard;
   unsigned i, port;
   uint16_t key, strong, weak;
   deterministic();
   snapshot_limits();
   input = xe_input_mailbox_new(); CHECK(input);
   packet_make(&packet, 1);
   for (port = 0; port < 4; ++port) CHECK(xe_input_packet_publish(input, port, &packet));
   guard = packet;
   CHECK(!xe_input_packet_read(input, 4, &guard) && guard.packet == 1);
   CHECK(!xe_input_packet_publish(input, 4, &packet));
   CHECK(!xe_input_packet_read(NULL, 0, &guard) && !xe_input_packet_read(input, 0, NULL));
   retro_atomic_store_release_int(&hold, 1);
   pinned = sthread_create(pinned_reader, NULL); CHECK(pinned);
   while (!retro_atomic_load_acquire_int(&entered)) {}
   for (i = 2; i < 10000; ++i) { packet_make(&packet, i); CHECK(xe_input_packet_publish(input, 0, &packet)); }
   retro_atomic_store_release_int(&resume_reader, 1); sthread_join(pinned);
   packet_make(&packet, 1);
   for (port = 0; port < 4; ++port) CHECK(xe_input_packet_publish(input, port, &packet));
   for (i = 0; i < READERS; ++i) { roles[i] = i; threads[i] = sthread_create(reader, &roles[i]); CHECK(threads[i]); }
   while (retro_atomic_load_acquire_int(&ready) != READERS) {}
   for (i = 1; i <= UPDATES; ++i) {
      packet_make(&packet, i);
      for (port = 0; port < 4; ++port) {
         CHECK(xe_input_packet_publish(input, port, &packet));
         if (xe_input_rumble_take(input, port, &strong, &weak)) CHECK(weak == (uint16_t)~strong);
      }
      key = (uint16_t)i; xe_input_keys_update(input, 0, &key, 1, 1);
   }
   retro_atomic_store_release_int(&stop, 1);
   for (i = 0; i < READERS; ++i) sthread_join(threads[i]);
   CHECK(!retro_atomic_load_acquire_int(&failed));
   xe_input_mailbox_free(input);
   puts("PASS: coherent pinned snapshots, six readers, 200000 publications, bounded multi-reader keys, frontend repeat and rumble pairs");
   return 0;
}

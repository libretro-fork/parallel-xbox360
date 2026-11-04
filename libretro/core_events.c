/* Fixed guest-to-frontend notices. MSVC C89-compatible. */
#include "core_events.h"
#include <retro_atomic.h>
#include <stdint.h>
#include <stddef.h>
#define NOTICE_SLOTS 8
#define NOTICE_BYTES 1024
/* States: free=0, exclusively writing=1, ready=2, borrowed=3.
 * Every payload access follows a successful ownership CAS. A producer may
 * replace an unread ready notice, but never a borrowed view or another writer. */
struct notice_slot {
   retro_atomic_int_t state;
   uint64_t ticket;
   char text[NOTICE_BYTES];
};
static retro_atomic_int_t title_stop, notice_pending;
static retro_atomic_64_t notice_ticket;
static struct notice_slot notices[NOTICE_SLOTS];
static uint64_t delivered_ticket;
void xe_libretro_request_title_stop(void) { retro_atomic_store_release_int(&title_stop, 1); }
int xe_libretro_take_title_stop(void) { return retro_atomic_exchange_int(&title_stop, 0); }
void xe_libretro_clear_title_stop(void) { retro_atomic_store_release_int(&title_stop, 0); }
static size_t append(char *out, size_t used, const char *text) {
   if (text) while (*text && used < NOTICE_BYTES - 1) out[used++] = *text++;
   return used;
}
static size_t utf8_tail(char *text, size_t used) {
   size_t start, need;
   unsigned lead;
   if (!used) return 0;
   start = used - 1;
   while (start && ((unsigned char)text[start] & 0xc0) == 0x80) --start;
   lead = (unsigned char)text[start];
   need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
   return used - start < need ? start : used;
}
int xe_libretro_notice_post(const char *prefix, const char *text) {
   struct notice_slot *slot;
   size_t used;
   uint64_t ticket;
   unsigned i;
   int state;
   if ((!prefix || !*prefix) && (!text || !*text)) return 0;
   for (i = 0; i < NOTICE_SLOTS; ++i) {
      slot = &notices[i];
      state = retro_atomic_load_relaxed_int(&slot->state);
      if ((state != 0 && state != 2) || !retro_atomic_cas_int(&slot->state, state, 1)) continue;
      do {
         ticket = (uint64_t)retro_atomic_load_relaxed_64(&notice_ticket);
      } while (!retro_atomic_cas_64(&notice_ticket, (int64_t)ticket, (int64_t)(ticket + 1)));
      slot->ticket = ticket + 1;
      used = 0;
      if (prefix && *prefix) {
         used = append(slot->text, used, prefix);
         used = append(slot->text, used, ": ");
      }
      used = append(slot->text, used, text);
      used = utf8_tail(slot->text, used);
      slot->text[used] = 0;
      retro_atomic_store_release_int(&slot->state, 2);
      retro_atomic_store_release_int(&notice_pending, 1);
      return 1;
   }
   return 0;
}
int xe_libretro_notice_acquire(xe_notice_view *view) {
   unsigned i, selected = NOTICE_SLOTS;
   uint64_t newest = delivered_ticket;
   struct notice_slot *slot;
   if (!view) return 0;
   view->text = NULL; view->slot = 0;
   if (!retro_atomic_load_relaxed_int(&notice_pending) ||
         !retro_atomic_exchange_int(&notice_pending, 0)) return 0;
   for (i = 0; i < NOTICE_SLOTS; ++i) {
      slot = &notices[i];
      if (!retro_atomic_cas_int(&slot->state, 2, 3)) continue;
      if ((int64_t)(slot->ticket - newest) > 0) {
         if (selected != NOTICE_SLOTS) retro_atomic_store_release_int(&notices[selected].state, 0);
         newest = slot->ticket; selected = i;
      } else retro_atomic_store_release_int(&slot->state, 0);
   }
   if (selected == NOTICE_SLOTS) return 0;
   /* A slow producer may publish an older ticket after a newer one was shown.
    * Keep the consumer watermark so delayed publications cannot reverse order. */
   delivered_ticket = newest;
   view->text = notices[selected].text;
   view->slot = selected + 1;
   return 1;
}
void xe_libretro_notice_release(xe_notice_view *view) {
   unsigned index;
   if (!view || !view->slot || view->slot > NOTICE_SLOTS) return;
   index = view->slot - 1;
   view->text = NULL; view->slot = 0;
   retro_atomic_store_release_int(&notices[index].state, 0);
}
void xe_libretro_notice_clear(void) {
   unsigned i;
   for (i = 0; i < NOTICE_SLOTS; ++i) retro_atomic_store_relaxed_int(&notices[i].state, 0);
   retro_atomic_store_relaxed_int(&notice_pending, 0);
   delivered_ticket = (uint64_t)retro_atomic_load_relaxed_64(&notice_ticket);
}

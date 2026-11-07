#include "core_disc.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <retro_atomic.h>
#include <file/file_path.h>
#ifndef XE_DISC_REALLOC
#define XE_DISC_REALLOC realloc
#define XE_DISC_FREE free
#endif
static int spill_append(xe_disc_lines *r, const char *data, size_t size) {
   size_t need, capacity; char *next;
   if (size > (size_t)-1 - r->spill_size - 1) return 0;
   need = r->spill_size + size + 1; capacity = r->spill_capacity;
   if (need > capacity) {
      if (!capacity) capacity = 4096;
      while (capacity < need) { if (capacity > (size_t)-1 / 2) { capacity = need; break; } capacity *= 2; }
      next = (char *)XE_DISC_REALLOC(r->spill, capacity); if (!next) return 0;
      r->spill = next; r->spill_capacity = capacity;
   }
   if (size) memcpy(r->spill + r->spill_size, data, size);
   r->spill_size += size; r->spill[r->spill_size] = 0; return 1;
}
int xe_disc_lines_open(xe_disc_lines *r, const char *path) {
   if (r->file || !path) return 0;
   r->file = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);
   r->used = r->at = r->spill_size = 0; r->line = 0; r->error = 0;
   return r->file != NULL;
}
int xe_disc_lines_next(xe_disc_lines *r, xe_disc_view *view) {
   view->data = NULL; view->size = 0; r->spill_size = 0;
   if (!r->file || r->error) return -1;
   for (;;) {
      if (r->at < r->used) {
         char *start = r->window + r->at;
         char *newline = (char *)memchr(start, '\n', r->used - r->at);
         size_t size = newline ? (size_t)(newline - start) : r->used - r->at;
         if (memchr(start, 0, size)) { r->error = 1; return -1; }
         r->at += size + (newline != NULL);
         if (newline) {
            *newline = 0;
            if (r->spill_size) {
               if (!spill_append(r, start, size)) { r->error = 1; return -1; }
               view->data = r->spill; view->size = r->spill_size;
            } else { view->data = start; view->size = size; }
            if (r->line != UINT_MAX) ++r->line;
            return 1;
         }
         if (!spill_append(r, start, size)) { r->error = 1; return -1; }
      }
      {
         int64_t got = filestream_read(r->file, r->window, 4096);
         if (got < 0 || got > 4096 || filestream_error(r->file)) { r->error = 1; return -1; }
         r->at = 0; r->used = (size_t)got; r->window[r->used] = 0;
         if (!got) {
            if (!r->spill_size) return 0;
            view->data = r->spill; view->size = r->spill_size; if (r->line != UINT_MAX) ++r->line;
            return 1;
         }
      }
   }
}
int xe_disc_lines_close(xe_disc_lines *r) {
   int ok = !r->error;
   if (r->file && filestream_close(r->file)) ok = 0;
   XE_DISC_FREE(r->spill); memset(r, 0, sizeof(*r)); return ok;
}
int xe_disc_line_path(xe_disc_view line, unsigned number, xe_disc_view *path) {
   size_t start = 0, end = line.size;
   if (number == 1 && end >= 3 && !memcmp(line.data, "\xef\xbb\xbf", 3)) start = 3;
   while (start < end && (line.data[start] == ' ' || line.data[start] == '\t')) ++start;
   while (end > start && (line.data[end-1] == '\r' || line.data[end-1] == ' ' || line.data[end-1] == '\t')) --end;
   if (start == end || line.data[start] == '#') return 0;
   path->data = line.data + start; path->size = end - start; return 1;
}
typedef struct disc_text { retro_atomic_int_t references; size_t length; } disc_text;
static char *text_new(size_t size) {
   disc_text *owner;
   if (size > (size_t)-1 - sizeof(*owner) - 1) return NULL;
   owner = (disc_text *)XE_DISC_REALLOC(NULL, sizeof(*owner) + size + 1);
   if (!owner) return NULL;
   retro_atomic_store_relaxed_int(&owner->references, 1); owner->length = size;
   return (char *)(owner + 1);
}
char *xe_disc_text_retain(char *text) {
   if (text) retro_atomic_fetch_add_int(&((disc_text *)text - 1)->references, 1);
   return text;
}
void xe_disc_text_release(char *text) {
   if (text) { disc_text *owner = (disc_text *)text - 1;
      if (retro_atomic_fetch_sub_int(&owner->references, 1) == 1) XE_DISC_FREE(owner);
   }
}
char *xe_disc_duplicate(const char *text) {
   size_t size; char *copy;
   if (!text) text = "";
   size = strlen(text); copy = text_new(size);
   if (copy) memcpy(copy, text, size + 1);
   return copy;
}
static size_t root_name(const char *path) {
#ifdef _WIN32
   size_t i;
   if (path[0] && path[1] == ':') return 2;
   if ((path[0] == '/' && path[1] == '/') || (path[0] == '\\' && path[1] == '\\')) {
      for (i = 2; path[i] && path[i] != '/' && path[i] != '\\'; ++i) {}
      return i;
   }
#else
   (void)path;
#endif
   return 0;
}
/* Preserve drive-rooted and drive-relative Windows joins; opaque VFS schemes
 * remain byte strings. No native realpath/stat or UTF-16 roundtrip. */
char *xe_disc_join(const char *reference, const char *path) {
   size_t prefix = 0, length, ref_root, input_root;
   char *joined;
   if (!reference || !path) return NULL;
   ref_root = root_name(reference); input_root = root_name(path);
#ifdef _WIN32
   if (!input_root && (path[0] == '/' || path[0] == '\\')) prefix = ref_root;
   else
#endif
   if (path_is_absolute(path)) return xe_disc_duplicate(path);
   else if (input_root && (input_root != ref_root || memcmp(reference, path, input_root))) return xe_disc_duplicate(path);
   else {
      prefix = (size_t)(path_basename_nocompression(reference) - reference);
      if (!prefix) prefix = ref_root;
      path += input_root;
   }
   length = strlen(path);
   if (prefix > (size_t)-1 - length - 1) return NULL;
   joined = text_new(prefix + length);
   if (joined) { if (prefix) memcpy(joined, reference, prefix); memcpy(joined + prefix, path, length + 1); }
   return joined;
}
xe_disc_view xe_disc_label(const char *path) {
   xe_disc_view view; const char *dot;
   view.data = path_basename_nocompression(path); view.size = strlen(view.data);
   dot = strrchr(view.data, '.');
   if (dot && dot != view.data && !(view.size == 2 && !memcmp(view.data, "..", 2))) view.size = (size_t)(dot - view.data);
   return view;
}
int xe_disc_paths_append(xe_disc_paths *paths, char *owned) {
   if (!owned || paths->count == (size_t)-1) return 0;
   if (paths->count == paths->capacity) {
      size_t capacity = paths->capacity ? paths->capacity * 2 : 8;
      char **items;
      if (capacity <= paths->capacity || capacity > (size_t)-1 / sizeof(*items)) return 0;
      items = (char **)XE_DISC_REALLOC(paths->items, capacity * sizeof(*items));
      if (!items) return 0;
      paths->items = items; paths->capacity = capacity;
   }
   paths->items[paths->count++] = owned; return 1;
}
int xe_disc_paths_replace(xe_disc_paths *paths, size_t index, const char *path) {
   char *copy;
   if (index >= paths->count || !(copy = xe_disc_duplicate(path))) return 0;
   xe_disc_text_release(paths->items[index]); paths->items[index] = copy; return 1;
}
void xe_disc_paths_remove(xe_disc_paths *paths, size_t index) {
   if (index >= paths->count) return;
   xe_disc_text_release(paths->items[index]); --paths->count;
   memmove(paths->items + index, paths->items + index + 1, (paths->count - index) * sizeof(*paths->items));
}
void xe_disc_paths_close(xe_disc_paths *paths) {
   size_t i; for (i = 0; i < paths->count; ++i) xe_disc_text_release(paths->items[i]);
   XE_DISC_FREE(paths->items); memset(paths, 0, sizeof(*paths));
}
int xe_disc_control_init(xe_disc_control *c) { if (!c->lock) c->lock = slock_new(); return c->lock != NULL; }
void xe_disc_control_close(xe_disc_control *c) { assert(!c->first); slock_free(c->lock); c->lock = NULL; }
void xe_disc_control_lock(xe_disc_control *c) { slock_lock(c->lock); }
void xe_disc_control_unlock(xe_disc_control *c) { slock_unlock(c->lock); }
void xe_disc_request_init(xe_disc_request *r, xe_disc_answer_fn answer, void *context) { r->next = NULL; r->answer = answer; r->context = context; r->pending = 0; }
int xe_disc_request_queue(xe_disc_control *c, xe_disc_request *r) {
   if (!r || !r->answer || r->pending) return 0;
   r->next = NULL; r->pending = 1;
   if (c->last) c->last->next = r; else c->first = r;
   c->last = r; return 1;
}
xe_disc_request *xe_disc_request_take(xe_disc_control *c) { xe_disc_request *r = c->first; c->first = c->last = NULL; return r; }
void xe_disc_request_complete(xe_disc_request *r, const char *path) {
   while (r) {
      xe_disc_request *next = r->next; xe_disc_answer_fn answer = r->answer; void *context = r->context;
      r->next = NULL; r->pending = 0; if (answer) answer(context, path); r = next;
   }
}

/* All frontend metadata edits use the common control lock. Immutable text has
 * independent atomic leases, so answers/notices run outside it without copies. */
static struct {
   xe_disc_control control;
   xe_disc_paths paths;
   size_t index, initial_index;
   char *initial_path;
   int ejected, closing;
   xe_disc_notice_fn notice;
} disc;
static char *current_path(void) {
   if (disc.index < disc.paths.count && disc.paths.items[disc.index][0])
      return xe_disc_text_retain(disc.paths.items[disc.index]);
   return disc.paths.count ? xe_disc_text_retain(disc.paths.items[0]) : NULL;
}
static int copy_view(char *out, size_t capacity, xe_disc_view view) {
   size_t bytes;
   if (!out || !capacity) return 0;
   bytes = view.size < capacity - 1 ? view.size : capacity - 1;
   if (bytes) memcpy(out, view.data, bytes);
   out[bytes] = 0; return 1;
}
int xe_disc_init(void) { return xe_disc_control_init(&disc.control); }
static bool RETRO_CALLCONV disc_set_eject(bool ejected) {
   xe_disc_request *pending = NULL; char *path = NULL; xe_disc_notice_fn notice;
   char message[1024];
   if (!disc.control.lock) return false;
   xe_disc_control_lock(&disc.control); disc.ejected = ejected; notice = disc.notice;
   if (!ejected && disc.control.first && disc.index < disc.paths.count && disc.paths.items[disc.index][0]) {
      pending = xe_disc_request_take(&disc.control); path = current_path();
   }
   xe_disc_control_unlock(&disc.control);
   if (pending) {
      xe_disc_view label = xe_disc_label(path);
      snprintf(message, sizeof(message), "Disc %.*s inserted", (int)(label.size < 900 ? label.size : 900), label.data);
      xe_disc_request_complete(pending, path);
      if (notice) notice(message);
      xe_disc_text_release(path);
   }
   return true;
}
static bool RETRO_CALLCONV disc_get_eject(void) {
   int value; if (!disc.control.lock) return false;
   xe_disc_control_lock(&disc.control); value = disc.ejected; xe_disc_control_unlock(&disc.control); return value != 0;
}
static unsigned RETRO_CALLCONV disc_get_index(void) {
   unsigned value; if (!disc.control.lock) return 0;
   xe_disc_control_lock(&disc.control); value = (unsigned)disc.index; xe_disc_control_unlock(&disc.control); return value;
}
static bool RETRO_CALLCONV disc_set_index(unsigned index) {
   int ok; if (!disc.control.lock) return false;
   xe_disc_control_lock(&disc.control); ok = index <= disc.paths.count;
   if (ok) disc.index = index;
   xe_disc_control_unlock(&disc.control); return ok != 0;
}
static unsigned RETRO_CALLCONV disc_get_count(void) {
   unsigned value; if (!disc.control.lock) return 0;
   xe_disc_control_lock(&disc.control); value = (unsigned)disc.paths.count; xe_disc_control_unlock(&disc.control); return value;
}
static bool RETRO_CALLCONV disc_replace(unsigned index, const struct retro_game_info *info) {
   int ok = 0; if (!disc.control.lock) return false;
   xe_disc_control_lock(&disc.control);
   if (index < disc.paths.count) {
      if (!info || !info->path) {
         xe_disc_paths_remove(&disc.paths, index);
         if (disc.index > index || disc.index > disc.paths.count) --disc.index;
         ok = 1;
      } else ok = xe_disc_paths_replace(&disc.paths, index, info->path);
   }
   xe_disc_control_unlock(&disc.control); return ok != 0;
}
static bool RETRO_CALLCONV disc_add(void) {
   int ok = 0; char *path;
   if (!disc.control.lock) return false;
   path = xe_disc_duplicate(""); if (!path) return false;
   xe_disc_control_lock(&disc.control);
   if (disc.paths.count < UINT_MAX) ok = xe_disc_paths_append(&disc.paths, path);
   xe_disc_control_unlock(&disc.control);
   if (!ok) xe_disc_text_release(path);
   return ok != 0;
}
static bool RETRO_CALLCONV disc_initial(unsigned index, const char *path) {
   char *copy, *old;
   if (!disc.control.lock || !(copy = xe_disc_duplicate(path))) return false;
   xe_disc_control_lock(&disc.control); old = disc.initial_path; disc.initial_path = copy; disc.initial_index = index;
   xe_disc_control_unlock(&disc.control); xe_disc_text_release(old); return true;
}
static bool RETRO_CALLCONV disc_path(unsigned index, char *out, size_t capacity) {
   int ok = 0; xe_disc_view view;
   if (!disc.control.lock) return false;
   xe_disc_control_lock(&disc.control);
   if (index < disc.paths.count && disc.paths.items[index][0]) {
      view.data = disc.paths.items[index]; view.size = ((disc_text *)view.data - 1)->length;
      ok = copy_view(out, capacity, view);
   }
   xe_disc_control_unlock(&disc.control); return ok != 0;
}
static bool RETRO_CALLCONV disc_label(unsigned index, char *out, size_t capacity) {
   int ok = 0; if (!disc.control.lock) return false;
   xe_disc_control_lock(&disc.control);
   if (index < disc.paths.count && disc.paths.items[index][0]) ok = copy_view(out, capacity, xe_disc_label(disc.paths.items[index]));
   xe_disc_control_unlock(&disc.control); return ok != 0;
}
void xe_disc_set_environment(retro_environment_t environment) {
   static const struct retro_disk_control_ext_callback extended = {
      disc_set_eject, disc_get_eject, disc_get_index, disc_set_index, disc_get_count,
      disc_replace, disc_add, disc_initial, disc_path, disc_label
   };
   static const struct retro_disk_control_callback basic = {
      disc_set_eject, disc_get_eject, disc_get_index, disc_set_index, disc_get_count, disc_replace, disc_add
   };
   if (!environment || !xe_disc_init()) return;
   if (!environment(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, (void *)&extended))
      environment(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, (void *)&basic);
}
static int playlist_extension(const char *path) {
   size_t size = strlen(path), i; const char extension[] = ".m3u";
   if (size < 4) return 0;
   for (i = 0; i < 4; ++i) { unsigned char ch = (unsigned char)path[size-4+i];
      if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch + ('a'-'A'));
      if (ch != (unsigned char)extension[i]) return 0;
   }
   return 1;
}
int xe_disc_load(const char *path, char *start, size_t capacity) {
   xe_disc_paths staged = {NULL, 0, 0}, old;
   char *initial; int ok = 1, result = 0;
   if (!start || !capacity) return 0;
   start[0] = 0;
   if (!xe_disc_init()) return 0;
   xe_disc_release();
   if (path && path[0]) {
      if (playlist_extension(path)) {
         xe_disc_lines reader; xe_disc_view line, token; int got = 0;
         memset(&reader, 0, sizeof(reader));
         if (!xe_disc_lines_open(&reader, path)) ok = 0;
         while (ok && (got = xe_disc_lines_next(&reader, &line)) > 0) {
            if (xe_disc_line_path(line, reader.line, &token)) {
               char *joined; ((char *)token.data)[token.size] = 0;
               joined = xe_disc_join(path, token.data);
               if (!joined || staged.count == UINT_MAX || !xe_disc_paths_append(&staged, joined)) {
                  xe_disc_text_release(joined); ok = 0;
               }
            }
         }
         if (got < 0) ok = 0;
         if (!xe_disc_lines_close(&reader)) ok = 0;
      } else {
         char *copy = xe_disc_duplicate(path);
         if (!copy || !xe_disc_paths_append(&staged, copy)) { xe_disc_text_release(copy); ok = 0; }
      }
   }
   if (!ok) xe_disc_paths_close(&staged);
   xe_disc_control_lock(&disc.control);
   old = disc.paths; disc.paths = staged; disc.index = 0; disc.ejected = 0; disc.closing = 0;
   if (disc.initial_path && disc.initial_index < disc.paths.count && !strcmp(disc.paths.items[disc.initial_index], disc.initial_path)) disc.index = disc.initial_index;
   initial = disc.initial_path; disc.initial_path = NULL; disc.initial_index = 0;
   if (disc.index < disc.paths.count) {
      const char *selected = disc.paths.items[disc.index]; size_t size = ((const disc_text *)selected - 1)->length;
      if (size && size < capacity) { memcpy(start, selected, size + 1); result = 1; }
   }
   xe_disc_control_unlock(&disc.control);
   xe_disc_paths_close(&old); xe_disc_text_release(initial); return result;
}
void xe_disc_release(void) {
   char *path; xe_disc_request *pending;
   if (!disc.control.lock) return;
   xe_disc_control_lock(&disc.control); disc.closing = 1; path = current_path(); pending = xe_disc_request_take(&disc.control);
   xe_disc_control_unlock(&disc.control);
   xe_disc_request_complete(pending, path ? path : ""); xe_disc_text_release(path);
}
void xe_disc_resume(void) {
   if (!disc.control.lock) return;
   xe_disc_control_lock(&disc.control); disc.closing = 0; xe_disc_control_unlock(&disc.control);
}
void xe_disc_unload(void) {
   xe_disc_paths old;
   if (!disc.control.lock) return;
   xe_disc_release(); xe_disc_control_lock(&disc.control);
   old = disc.paths; memset(&disc.paths, 0, sizeof(disc.paths)); disc.index = 0; disc.ejected = 0;
   xe_disc_control_unlock(&disc.control); xe_disc_paths_close(&old);
}
void xe_disc_deinit(void) {
   if (!disc.control.lock) return;
   xe_disc_unload(); xe_disc_text_release(disc.initial_path); disc.initial_path = NULL;
   xe_disc_control_close(&disc.control); memset(&disc, 0, sizeof(disc));
}
void xe_disc_set_notice(xe_disc_notice_fn notice) {
   if (!xe_disc_init()) return;
   xe_disc_control_lock(&disc.control); disc.notice = notice; xe_disc_control_unlock(&disc.control);
}
void xe_disc_dispatch(uint8_t number, const char *message, xe_disc_request *request) {
   char *path = NULL; int closing; xe_disc_notice_fn notice; char text[1024];
   if (!request || !request->answer) return;
   if (!disc.control.lock) { xe_disc_request_complete(request, ""); return; }
   xe_disc_control_lock(&disc.control); closing = disc.closing; notice = disc.notice;
   if (closing) path = current_path();
   else if ((!message || !strstr(message, "ERROR:")) && number && number <= disc.paths.count && disc.paths.items[number-1][0]) {
      disc.index = number-1; disc.ejected = 0; path = current_path();
   } else xe_disc_request_queue(&disc.control, request);
   xe_disc_control_unlock(&disc.control);
   if (closing || path) {
      if (!closing && notice) { xe_disc_view label = xe_disc_label(path);
         snprintf(text, sizeof(text), "Disc %u inserted (%.*s)", (unsigned)number, (int)(label.size < 900 ? label.size : 900), label.data);
      }
      xe_disc_request_complete(request, path ? path : "");
      if (!closing && notice) notice(text);
      xe_disc_text_release(path); return;
   }
   snprintf(text, sizeof(text), "%sdisc %u in Disc Control and close the tray", message && strstr(message, "ERROR:") ? "Wrong disc - pick " : "The game asks for ", (unsigned)number);
   if (notice) notice(text);
}

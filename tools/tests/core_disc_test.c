/* Real VFS + disc callbacks, hostile reads/OOM and answer lifetimes; C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static size_t allocations, live_blocks;
static long fail_after = -1;
static void *disc_realloc(void *old, size_t size) {
   void *next;
   if (fail_after == 0) return NULL;
   if (fail_after > 0) --fail_after;
   ++allocations; next = realloc(old, size);
   if (next && !old) ++live_blocks;
   return next;
}
static void disc_free(void *pointer) {
   if (pointer) { --live_blocks; free(pointer); }
}
#define XE_DISC_REALLOC disc_realloc
#define XE_DISC_FREE disc_free
#include "../../libretro/core_disc.c"
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #expr); exit(1); } } while (0)
static unsigned char data[24000];
static size_t length, position, short_read = 4096;
static unsigned opens, closes, seeks, reads;
static int fault;
static struct retro_disk_control_ext_callback ext;
static struct retro_disk_control_callback basic;
static int extended = 1, registered;
static struct retro_vfs_file_handle *RETRO_CALLCONV vopen(const char *path, unsigned mode, unsigned hints) {
   (void)hints;
   if (strcmp(path, "disc://games/list.m3u") || mode != RETRO_VFS_FILE_ACCESS_READ || fault == 1) return NULL;
   position = 0; ++opens; return (struct retro_vfs_file_handle *)data;
}
static int RETRO_CALLCONV vclose(struct retro_vfs_file_handle *file) {
   (void)file; ++closes; return fault == 2 ? -1 : 0;
}
static int64_t RETRO_CALLCONV vsize(struct retro_vfs_file_handle *file) { (void)file; return (int64_t)length; }
static int64_t RETRO_CALLCONV vtell(struct retro_vfs_file_handle *file) { (void)file; return (int64_t)position; }
static int64_t RETRO_CALLCONV vseek(struct retro_vfs_file_handle *file, int64_t offset, int whence) {
   (void)file; (void)offset; (void)whence; ++seeks; return -1;
}
static int64_t RETRO_CALLCONV vread(struct retro_vfs_file_handle *file, void *out, uint64_t amount) {
   size_t n = amount > short_read ? short_read : (size_t)amount;
   (void)file; ++reads;
   if (fault == 3 && position >= 8) { errno = EIO; return -1; }
   if (n > length - position) n = length - position;
   if (n) memcpy(out, data + position, n);
   position += n; return (int64_t)n;
}
static bool RETRO_CALLCONV environment(unsigned command, void *value) {
   if (command == RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE) {
      if (!extended) return false;
      ext = *(struct retro_disk_control_ext_callback *)value; ++registered; return true;
   }
   if (command == RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE) {
      basic = *(struct retro_disk_control_callback *)value; ++registered; return true;
   }
   return false;
}
static void source(const char *text) { length = strlen(text); memcpy(data, text, length); }
static void interfaces(void) {
   struct retro_vfs_interface iface;
   struct retro_vfs_interface_info info;
   memset(&iface, 0, sizeof(iface));
   iface.open = vopen; iface.close = vclose; iface.size = vsize;
   iface.tell = vtell; iface.seek = vseek; iface.read = vread;
   info.required_interface_version = FILESTREAM_REQUIRED_VFS_VERSION; info.iface = &iface;
   filestream_vfs_init(&info);
   xe_disc_set_environment(environment); CHECK(registered == 1);
   extended = 0; xe_disc_set_environment(environment);
   CHECK(registered == 2 && basic.get_num_images == ext.get_num_images);
}
static void reader_tests(void) {
   xe_disc_lines reader; xe_disc_view view;
   size_t before;
   memset(&reader, 0, sizeof(reader));
   source("one\ntwo\n"); short_read = 4096; before = allocations;
   CHECK(xe_disc_lines_open(&reader, "disc://games/list.m3u"));
   CHECK(xe_disc_lines_next(&reader, &view) == 1 && view.size == 3 && view.data == reader.window);
   CHECK(xe_disc_lines_next(&reader, &view) == 1 && view.data == reader.window + 4);
   CHECK(xe_disc_lines_next(&reader, &view) == 0 && allocations == before && reader.spill == NULL);
   CHECK(xe_disc_lines_close(&reader));
   memset(data, 'x', 9000); data[9000] = '\n'; memset(data + 9001, 'y', 9000); length = 18001;
   CHECK(xe_disc_lines_open(&reader, "disc://games/list.m3u"));
   CHECK(xe_disc_lines_next(&reader, &view) == 1 && view.size == 9000 && view.data == reader.spill);
   before = allocations;
   CHECK(xe_disc_lines_next(&reader, &view) == 1 && view.size == 9000 && view.data == reader.spill);
   CHECK(allocations == before && view.data[0] == 'y' && view.data[8999] == 'y');
   CHECK(xe_disc_lines_next(&reader, &view) == 0 && xe_disc_lines_close(&reader));
}
static void playlist_tests(void) {
   char start[24000], out[128]; size_t step, n; unsigned before;
   static const char playlist[] = "\xef\xbb\xbf  # heading\r\n \t\r\n one.iso \t\r\n../two.iso\nother://disc/three.iso";
   for (step = 1; step <= 4096; step *= 2) {
      short_read = step; source(playlist);
      CHECK(xe_disc_load("disc://games/list.m3u", start, sizeof(start)));
      CHECK(!strcmp(start, "disc://games/one.iso") && ext.get_num_images() == 3);
      CHECK(ext.get_image_path(1, out, sizeof(out)) && !strcmp(out, "disc://games/../two.iso"));
      CHECK(ext.get_image_path(2, out, sizeof(out)) && !strcmp(out, "other://disc/three.iso"));
      CHECK(ext.set_initial_image(1, "disc://games/../two.iso"));
      CHECK(xe_disc_load("disc://games/list.m3u", start, sizeof(start)) && !strcmp(start, "disc://games/../two.iso"));
      CHECK(ext.get_image_index() == 1);
   }
   memset(out, 0x55, sizeof(out)); CHECK(ext.get_image_path(0, out, 4));
   CHECK(!memcmp(out, "dis\0", 4) && out[4] == 0x55);
   CHECK(!ext.get_image_path(0, out, 0) && !ext.get_image_path(0, NULL, 8));
   CHECK(ext.get_image_label(0, out, sizeof(out)) && !strcmp(out, "one"));
   CHECK(ext.set_image_index(3) && !ext.set_image_index(4));
   CHECK(ext.replace_image_index(0, NULL) && ext.get_image_index() == 2);
   CHECK(ext.add_image_index() && ext.get_num_images() == 3 && !ext.get_image_path(2, out, sizeof(out)));
   {
      struct retro_game_info info;
      memset(&info, 0, sizeof(info)); info.path = "new.iso";
      CHECK(ext.replace_image_index(2, &info)); CHECK(ext.get_image_path(2, out, sizeof(out)) && !strcmp(out, "new.iso"));
   }
   short_read = 4;
   for (fault = 1; fault <= 3; ++fault) {
      source("a.iso\nb.iso\n"); before = opens - closes;
      CHECK(!xe_disc_load("disc://games/list.m3u", start, sizeof(start)) && !start[0] && !ext.get_num_images());
      CHECK(opens - closes == before);
   }
   fault = 0; source("a.iso\nb.iso\n"); data[7] = 0;
   CHECK(!xe_disc_load("disc://games/list.m3u", start, sizeof(start)) && !ext.get_num_images());
   /* Fail every module allocation in turn, including pointer growth and spill. */
   short_read = 3;
   for (n = 0; n < 15; ++n) {
      int ok;
      source("a.iso\nb.iso\n"); fail_after = (long)n;
      ok = xe_disc_load("disc://games/list.m3u", start, sizeof(start)); fail_after = -1;
      if (!ok) CHECK(!ext.get_num_images());
      xe_disc_unload(); CHECK(live_blocks == 0 && opens == closes);
   }
   CHECK(!xe_disc_load("long-path.iso", start, 4) && !start[0]);
   xe_disc_unload();
   CHECK(seeks == 0 && reads > 0);
}
#ifdef XE_TEST_DISC_PATH_REFERENCE
extern int xe_disc_path_reference(const char *, const char *, char *, size_t);
#endif
static void path_tests(void) {
   static const char *cases[][3] = {
      {"list.m3u", "one.iso", "one.iso"}, {"/games/list.m3u", "../one.iso", "/games/../one.iso"},
      {"disc://games/list.m3u", "smb://share/one.iso", "smb://share/one.iso"},
#ifdef _WIN32
      {"C:\\games\\list.m3u", "\\one.iso", "C:\\one.iso"},
      {"C:\\games\\list.m3u", "C:one.iso", "C:\\games\\one.iso"},
      {"C:\\games\\list.m3u", "D:one.iso", "D:one.iso"},
      {"C:list.m3u", "one.iso", "C:one.iso"},
      {"\\\\host\\share\\list.m3u", "\\one.iso", "\\\\host\\one.iso"},
#endif
      {"/games/list.m3u", "\xc3\xa9.iso", "/games/\xc3\xa9.iso"}
   };
#ifdef XE_TEST_DISC_PATH_REFERENCE
   char expected[512], actual[512];
   size_t j;
#endif
   size_t i;
   for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
      char *joined = xe_disc_join(cases[i][0], cases[i][1]);
      CHECK(joined && !strcmp(joined, cases[i][2]));
#ifdef XE_TEST_DISC_PATH_REFERENCE
      /* URI joining is deliberately frontend-VFS-aware rather than host-OS
       * interpretation of a scheme. Physical joins match the prior boundary. */
      if (!strstr(cases[i][0], "://") && !strstr(cases[i][1], "://")
#ifdef __MINGW32__
            /* MinGW's legacy std::filesystem drops the UNC server when joining
             * a rooted path. The golden result above matches native MSVC. */
            && strncmp(cases[i][0], "\\\\", 2)
#endif
            ) {
         CHECK(xe_disc_path_reference(cases[i][0], cases[i][1], expected, sizeof(expected)));
         CHECK(strlen(joined) < sizeof(actual)); strcpy(actual, joined);
#ifdef _WIN32
         for (j = 0; actual[j]; ++j) if (actual[j] == '\\') actual[j] = '/';
         for (j = 0; expected[j]; ++j) if (expected[j] == '\\') expected[j] = '/';
#else
         (void)j;
#endif
         if (strcmp(actual, expected)) fprintf(stderr, "join %lu: '%s' vs '%s'\n", (unsigned long)i, actual, expected);
         CHECK(!strcmp(actual, expected));
      }
#endif
      xe_disc_text_release(joined);
   }
   { xe_disc_view label = xe_disc_label("some/path/.hidden"); CHECK(label.size == 7 && !memcmp(label.data, ".hidden", 7)); }
   { xe_disc_view label = xe_disc_label(".."); CHECK(label.size == 2); }
}
static unsigned answered, notices;
static int reenter;
static void notice(const char *text) { CHECK(text && text[0]); ++notices; }
struct answer_state { xe_disc_request request; unsigned sequence; };
static void answer(void *opaque, const char *path) {
   struct answer_state *state = (struct answer_state *)opaque;
   CHECK(state->sequence == answered && !state->request.pending && path);
   ++answered; free(state); /* Completion must never read its node again. */
   if (reenter) {
      xe_disc_unload();
      CHECK(!strcmp(path, "disc.iso")); /* Leased bytes survive table teardown. */
   }
}
static struct answer_state *new_answer(unsigned sequence) {
   struct answer_state *state = (struct answer_state *)malloc(sizeof(*state)); CHECK(state);
   state->sequence = sequence; xe_disc_request_init(&state->request, answer, state); return state;
}
static void callback_tests(void) {
   char start[128]; unsigned i;
   CHECK(xe_disc_load("disc.iso", start, sizeof(start))); xe_disc_set_notice(notice);
   for (i = 0; i < 8; ++i) { struct answer_state *state = new_answer(i); xe_disc_dispatch(2, "ERROR:", &state->request); }
   CHECK(answered == 0 && notices == 8);
   reenter = 1; CHECK(ext.set_eject_state(false)); CHECK(answered == 8 && live_blocks == 0); reenter = 0;
   { struct answer_state *state = new_answer(8); xe_disc_dispatch(1, "", &state->request); CHECK(answered == 9); }
   CHECK(xe_disc_load("disc.iso", start, sizeof(start)));
   for (i = 9; i < 17; ++i) { struct answer_state *state = new_answer(i); xe_disc_dispatch(2, "", &state->request); }
   xe_disc_release(); CHECK(answered == 17);
   { struct answer_state *state = new_answer(17); xe_disc_dispatch(2, "", &state->request); CHECK(answered == 18); }
   xe_disc_resume();
   { struct answer_state *state = new_answer(18); xe_disc_dispatch(2, "", &state->request); CHECK(answered == 18); }
   CHECK(xe_disc_load("other.iso", start, sizeof(start)) && answered == 19);
   xe_disc_unload(); CHECK(live_blocks == 0);
}
/* Four real guest producers publish stack-owned requests while the frontend
 * releases/drains. Answer publication is the final access to each context. */
#define THREAD_EPOCHS 400
struct thread_state { retro_atomic_int_t ready, done, error; };
static retro_atomic_int_t permit;
static void thread_answer(void *opaque, const char *path) {
   struct thread_state *state = (struct thread_state *)opaque;
   if (strcmp(path, "disc.iso")) retro_atomic_store_relaxed_int(&state->error, 1);
   retro_atomic_fetch_add_int(&state->done, 1);
}
static void producer(void *opaque) {
   struct thread_state *state = (struct thread_state *)opaque;
   int epoch;
   for (epoch = 0; epoch < THREAD_EPOCHS; ++epoch) {
      xe_disc_request request;
      while (retro_atomic_load_acquire_int(&permit) != epoch) {}
      xe_disc_request_init(&request, thread_answer, state);
      xe_disc_dispatch(2, "ERROR:", &request);
      retro_atomic_store_release_int(&state->ready, epoch + 1);
      while (retro_atomic_load_acquire_int(&state->done) != epoch + 1) {}
   }
}
static void thread_tests(void) {
   struct thread_state states[4]; sthread_t *threads[4];
   int epoch, i; char start[64];
   memset(states, 0, sizeof(states));
   CHECK(xe_disc_load("disc.iso", start, sizeof(start))); xe_disc_set_notice(NULL);
   for (i = 0; i < 4; ++i) { threads[i] = sthread_create(producer, &states[i]); CHECK(threads[i]); }
   for (epoch = 0; epoch < THREAD_EPOCHS; ++epoch) {
      for (i = 0; i < 4; ++i) while (retro_atomic_load_acquire_int(&states[i].ready) != epoch + 1) {}
      /* Ordinary tray completion and shutdown cancellation both drive the
       * exact same retiring-node algorithm. Late requests answer immediately. */
      if (epoch & 1) xe_disc_release(); else CHECK(ext.set_eject_state(false));
      for (i = 0; i < 4; ++i) while (retro_atomic_load_acquire_int(&states[i].done) < epoch + 1) {}
      xe_disc_resume();
      retro_atomic_store_release_int(&permit, epoch + 1);
   }
   for (i = 0; i < 4; ++i) { sthread_join(threads[i]); CHECK(!retro_atomic_load_relaxed_int(&states[i].error)); }
   xe_disc_unload();
}int main(void) {
   interfaces(); reader_tests(); playlist_tests(); path_tests(); callback_tests(); thread_tests();
   xe_disc_deinit(); CHECK(!live_blocks && opens == closes);
   puts("disc: streaming VFS, OOM, paths, basic/ext control, FIFO, retiring/reentrant answers and 1600 threaded requests passed");
   return 0;
}

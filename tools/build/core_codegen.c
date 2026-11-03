/* Native build-input generator for parallel_xbox360. MSVC C89. */
#ifndef _WIN32
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

unsigned char *core_deflate_compress(unsigned char *data, int size, int *out_size, int quality);

typedef struct Buffer {
  char *data;
  size_t size;
  size_t capacity;
} Buffer;

typedef struct Files {
  char **names;
  size_t count;
} Files;

static void fail(const char *message, const char *path) {
  fprintf(stderr, "core-codegen: %s: %s\n", message, path);
  exit(EXIT_FAILURE);
}

static void *allocate(size_t size) {
  void *result;
  result = malloc(size ? size : 1);
  if (!result) fail("out of memory", "");
  return result;
}

static char *duplicate(const char *text) {
  char *result;
  result = (char *)allocate(strlen(text) + 1);
  strcpy(result, text);
  return result;
}

#ifdef _WIN32
static wchar_t *wide_path(const char *path) {
  int count;
  wchar_t *result;
  count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (!count) fail("invalid UTF-8 path", path);
  result = (wchar_t *)allocate((size_t)count * sizeof(*result));
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, result, count))
    fail("cannot convert path", path);
  return result;
}

static char *utf8_path(const wchar_t *path) {
  int count;
  char *result;
  count = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
  if (!count) fail("cannot encode path", "");
  result = (char *)allocate((size_t)count);
  if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, result, count, NULL, NULL))
    fail("cannot encode path", "");
  return result;
}
#endif

static FILE *open_file(const char *path, const char *mode) {
#ifdef _WIN32
  wchar_t *name;
  wchar_t *open_mode;
  FILE *file;
  name = wide_path(path);
  open_mode = wide_path(mode);
  file = _wfopen(name, open_mode);
  free(open_mode); free(name);
  return file;
#else
  return fopen(path, mode);
#endif
}

static void append(Buffer *buffer, const void *data, size_t size) {
  size_t needed;
  size_t capacity;
  char *replacement;
  if (size > (size_t)-1 - buffer->size - 1) fail("input too large", "");
  needed = buffer->size + size + 1;
  if (needed > buffer->capacity) {
    capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < needed) {
      if (capacity > (size_t)-1 / 2) {
        capacity = needed;
        break;
      }
      capacity *= 2;
    }
    replacement = (char *)realloc(buffer->data, capacity);
    if (!replacement) fail("out of memory", "");
    buffer->data = replacement;
    buffer->capacity = capacity;
  }
  if (size) memcpy(buffer->data + buffer->size, data, size);
  buffer->size += size;
  buffer->data[buffer->size] = 0;
}

static void text(Buffer *buffer, const char *value) {
  append(buffer, value, strlen(value));
}

static void number(Buffer *buffer, size_t value) {
  char digits[32];
  if (value > ULONG_MAX) fail("value too large", "");
  sprintf(digits, "%lu", (unsigned long)value);
  text(buffer, digits);
}

static char *join(const char *directory, const char *name) {
  Buffer result = {0, 0, 0};
  text(&result, directory);
  if (result.size && result.data[result.size - 1] != '/') text(&result, "/");
  text(&result, name);
  return result.data;
}

static Buffer read_file(const char *path, int required) {
  FILE *file;
  long size = 0;
  Buffer result = {0, 0, 0};
  file = open_file(path, "rb");
  if (!file) {
    if (required) fail("cannot read", path);
    return result;
  }
  if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET)) fail("cannot size input", path);
  result.capacity = (size_t)size + 1;
  result.data = (char *)allocate(result.capacity);
  result.size = (size_t)size;
  if (result.size && fread(result.data, 1, result.size, file) != result.size)
    fail("cannot read input", path);
  result.data[result.size] = 0;
  if (fclose(file)) fail("cannot close input", path);
  return result;
}

static void parents(const char *path) {
  char *copy;
  char *cursor;
#ifdef _WIN32
  wchar_t *wide;
  int status;
#endif
  copy = duplicate(path);
  for (cursor = copy; *cursor; ++cursor) {
    if (*cursor != '/' && *cursor != '\\') continue;
    if (cursor == copy || cursor[-1] == ':') continue;
    *cursor = 0;
#ifdef _WIN32
    wide = wide_path(copy);
    status = _wmkdir(wide);
    free(wide);
    if (status && errno != EEXIST) fail("cannot create directory", copy);
#else
    if (mkdir(copy, 0777) && errno != EEXIST) fail("cannot create directory", copy);
#endif
    *cursor = '/';
  }
  free(copy);
}

static void write_file(const char *path, const Buffer *value) {
  Buffer previous;
  FILE *file;
  previous = read_file(path, 0);
  if (previous.data && previous.size == value->size &&
      !memcmp(previous.data, value->data, value->size)) {
    free(previous.data);
    return;
  }
  free(previous.data);
  parents(path);
  file = open_file(path, "wb");
  if (!file) fail("cannot write", path);
  if (value->size && fwrite(value->data, 1, value->size, file) != value->size)
    fail("cannot write output", path);
  if (fclose(file)) fail("cannot close output", path);
}

static void output(const char *directory, const char *name, Buffer *value) {
  char *path;
  path = join(directory, name);
  write_file(path, value);
  free(path);
  free(value->data);
  memset(value, 0, sizeof(*value));
}

static void replace(Buffer *value, const char *from, const char *to) {
  Buffer result = {0, 0, 0};
  const char *cursor;
  const char *match;
  cursor = value->data;
  while ((match = strstr(cursor, from)) != NULL) {
    append(&result, cursor, (size_t)(match - cursor));
    text(&result, to);
    cursor = match + strlen(from);
  }
  text(&result, cursor);
  free(value->data);
  *value = result;
}

static void quoted(Buffer *value, const char *string) {
  const unsigned char *cursor;
  char octal[8];
  text(value, "\"");
  for (cursor = (const unsigned char *)string; *cursor; ++cursor) {
    if (*cursor == '"' || *cursor == '\\') text(value, "\\");
    if (*cursor >= 32 && *cursor < 127) append(value, cursor, 1);
    else {
      sprintf(octal, "\\%03o", (unsigned int)*cursor);
      text(value, octal);
    }
  }
  text(value, "\"");
}

static void bytes(Buffer *value, const unsigned char *data, size_t size,
                  int terminator) {
  size_t i;
  for (i = 0; i < size; ++i) {
    if (!(i % 32)) text(value, "\n ");
    number(value, data[i]);
    text(value, ",");
  }
  if (terminator) text(value, "0");
  text(value, "\n");
}

static void add_file(Files *files, const char *name) {
  char **replacement;
  replacement = (char **)realloc(files->names,
                                 (files->count + 1) * sizeof(*files->names));
  if (!replacement) fail("out of memory", name);
  files->names = replacement;
  files->names[files->count++] = duplicate(name);
}

static void list_files(Files *files, const char *root, const char *relative,
                       int recursive) {
  char *directory;
  char *child;
  char *full;
  const char *name;
#ifdef _WIN32
  struct _stat info;
  WIN32_FIND_DATAW entry;
  HANDLE handle;
  char *pattern;
  char *allocated_name;
  wchar_t *wide;
  int status;
#else
  struct stat info;
  DIR *handle;
  struct dirent *entry;
#endif
  directory = join(root, relative);
#ifdef _WIN32
  pattern = join(directory, "*");
  wide = wide_path(pattern);
  handle = FindFirstFileW(wide, &entry);
  free(wide);
  free(pattern);
  if (handle == INVALID_HANDLE_VALUE) fail("cannot list directory", directory);
  do {
    allocated_name = utf8_path(entry.cFileName);
    name = allocated_name;
#else
  handle = opendir(directory);
  if (!handle) fail("cannot list directory", directory);
  while ((entry = readdir(handle)) != NULL) {
    name = entry->d_name;
#endif
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
#ifdef _WIN32
      free(allocated_name);
#endif
      continue;
    }
    child = join(relative, name);
    full = join(root, child);
#ifdef _WIN32
    wide = wide_path(full);
    status = _wstat(wide, &info);
    free(wide);
    if (status) fail("cannot stat file", full);
#else
    if (stat(full, &info)) fail("cannot stat file", full);
#endif
    if ((info.st_mode & S_IFMT) == S_IFDIR) {
      if (recursive) list_files(files, root, child, recursive);
    } else if ((info.st_mode & S_IFMT) == S_IFREG) add_file(files, child);
    free(full);
    free(child);
#ifdef _WIN32
    free(allocated_name);
  } while (FindNextFileW(handle, &entry));
  FindClose(handle);
#else
  }
  closedir(handle);
#endif
  free(directory);
}

static int compare_files(const void *left, const void *right) {
  return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static void sort_files(Files *files) {
  qsort(files->names, files->count, sizeof(*files->names), compare_files);
}

static void free_files(Files *files) {
  size_t i;
  for (i = 0; i < files->count; ++i) free(files->names[i]);
  free(files->names);
}

static void u32(Buffer *value, size_t number_value) {
  unsigned char encoded[4];
  unsigned long value32;
  int i;
  if (number_value > 0xffffffffUL) fail("bundle field exceeds 32 bits", "");
  value32 = (unsigned long)number_value;
  for (i = 0; i < 4; ++i) {
    encoded[i] = (unsigned char)(value32 & 255);
    value32 >>= 8;
  }
  append(value, encoded, 4);
}

static void bundle(const char *out) {
  const char *root = "libretro/data/patches";
  Files files = {NULL, 0};
  Buffer raw = {0, 0, 0};
  Buffer file;
  Buffer source = {0, 0, 0};
  Buffer header = {0, 0, 0};
  unsigned char *compressed;
  int compressed_size;
  size_t i;
  char *path;
  list_files(&files, root, "", 1);
  sort_files(&files);
  u32(&raw, files.count);
  for (i = 0; i < files.count; ++i) {
    path = join(root, files.names[i]);
    file = read_file(path, 1);
    u32(&raw, strlen(files.names[i]));
    text(&raw, files.names[i]);
    u32(&raw, file.size);
    append(&raw, file.data, file.size);
    free(file.data);
    free(path);
  }
  if (raw.size > INT_MAX) fail("patch bundle too large", root);
  compressed = core_deflate_compress((unsigned char *)raw.data, (int)raw.size,
                                 &compressed_size, 9);
  if (!compressed) fail("cannot compress patch bundle", root);
  text(&header, "#pragma once\n#include <cstddef>\nnamespace xe::embedded_bundle_patches {\nextern const unsigned char kBundleData[];\nextern const size_t kBundleSize;\n}\n");
  text(&source, "#include \"embedded_bundle_patches.h\"\nnamespace xe::embedded_bundle_patches {\nconst unsigned char kBundleData[] = {");
  bytes(&source, compressed, (size_t)compressed_size, 0);
  text(&source, "};\nconst size_t kBundleSize = sizeof(kBundleData);\n}\n");
  output(out, "embedded_bundle_patches.h", &header);
  output(out, "embedded_bundle_patches.cc", &source);
  free(compressed);
  free(raw.data);
  free_files(&files);
}

static void fonts(const char *out) {
  Files files = {NULL, 0};
  Buffer header = {0, 0, 0};
  Buffer source = {0, 0, 0};
  Buffer entries = {0, 0, 0};
  Buffer file;
  char *path;
  char *symbol;
  char *cursor;
  size_t i;
  list_files(&files, "assets/font", "", 0);
  sort_files(&files);
  text(&header, "#pragma once\n#include <cstddef>\nnamespace xe::ui::embedded_font {\nstruct Asset { const char *name; const unsigned char *data; size_t size; };\n");
  text(&source, "#include \"embedded_font.h\"\nnamespace xe::ui::embedded_font {\n");
  for (i = 0; i < files.count; ++i) {
    symbol = (char *)allocate(strlen(files.names[i]) + 2);
    symbol[0] = 0;
    if (files.names[i][0] >= '0' && files.names[i][0] <= '9') strcpy(symbol, "_");
    strcat(symbol, files.names[i]);
    for (cursor = symbol; *cursor; ++cursor)
      if (!((*cursor >= 'A' && *cursor <= 'Z') ||
            (*cursor >= 'a' && *cursor <= 'z') ||
            (*cursor >= '0' && *cursor <= '9'))) *cursor = '_';
    path = join("assets/font", files.names[i]);
    file = read_file(path, 1);
    text(&header, "extern const unsigned char "); text(&header, symbol);
    text(&header, "_data[];\nextern const size_t "); text(&header, symbol);
    text(&header, "_size;\n");
    text(&source, "const unsigned char "); text(&source, symbol);
    text(&source, "_data[] = {"); bytes(&source, (unsigned char *)file.data, file.size, 1);
    text(&source, "};\nconst size_t "); text(&source, symbol);
    text(&source, "_size = "); number(&source, file.size); text(&source, ";\n");
    if (i) text(&entries, ",\n");
    text(&entries, "{"); quoted(&entries, files.names[i]); text(&entries, ",");
    text(&entries, symbol); text(&entries, "_data,"); number(&entries, file.size);
    text(&entries, "}");
    free(file.data); free(path); free(symbol);
  }
  text(&header, "extern const Asset kAll[];\nextern const size_t kAllCount;\n}\n");
  text(&source, "const Asset kAll[] = {\n"); text(&source, entries.data);
  text(&source, "};\nconst size_t kAllCount = sizeof(kAll) / sizeof(kAll[0]);\n}\n");
  output(out, "embedded_font.h", &header);
  output(out, "embedded_font.cc", &source);
  free(entries.data); free_files(&files);
}

static int enabled(const char *name, int windows, int arm64, int msvc) {
  const char *common[] = {"HAVE_ATTRIBUTE_ALWAYS_INLINE", "HAVE_BUILTIN_CTZ",
    "HAVE_BUILTIN_EXPECT", "HAVE_BUILTIN_PREFETCH", NULL};
  const char *posix[] = {"HAVE_FUNC_MMAP", "HAVE_FUNC_SYSCONF", "HAVE_SYS_MMAN_H",
    "HAVE_SYS_RESOURCE_H", "HAVE_SYS_TIME_H", "HAVE_SYS_UIO_H", "HAVE_UNISTD_H", NULL};
  int i;
  for (i = 0; common[i]; ++i) if (!strcmp(name, common[i])) return !msvc;
  if (!windows)
    for (i = 0; posix[i]; ++i) if (!strcmp(name, posix[i])) return 1;
  return (windows && !strcmp(name, "HAVE_WINDOWS_H")) ||
    (arm64 && !strcmp(name, "SNAPPY_HAVE_NEON")) ||
    (!arm64 && (!strcmp(name, "SNAPPY_HAVE_SSSE3") ||
                !strcmp(name, "SNAPPY_HAVE_X86_CRC32")));
}

static void configs(const char *out, int windows, int arm64, int msvc) {
  const char *zlib_names[] = {"zlib-ng.h", "zconf-ng.h", "zlib_name_mangling-ng.h", "gzread.c", NULL};
  Buffer file;
  Buffer result = {0, 0, 0};
  char *path;
  char *name;
  char digits[32];
  char token[128];
  const char *cursor;
  const char *match;
  const char *end;
  unsigned int major = 0;
  unsigned int minor = 0;
  unsigned int patch = 0;
  int i;
  for (i = 0; zlib_names[i]; ++i) {
    name = (char *)allocate(strlen(zlib_names[i]) + 4);
    strcpy(name, zlib_names[i]); strcat(name, ".in");
    path = join("third_party/zlib-ng",
                !strcmp(zlib_names[i], "zlib_name_mangling-ng.h")
                    ? "zlib_name_mangling.h.empty" : name);
    file = read_file(path, 1); free(name); free(path);
    replace(&file, "@ZLIB_SYMBOL_PREFIX@", "");
    path = join(out, "third_party/zlib-ng");
    output(path, zlib_names[i], &file); free(path);
  }
  file = read_file("third_party/snappy/CMakeLists.txt", 1);
  match = strstr(file.data, "project(Snappy VERSION ");
  if (!match || sscanf(match, "project(Snappy VERSION %u.%u.%u", &major, &minor, &patch) != 3)
    fail("cannot read Snappy version", "third_party/snappy/CMakeLists.txt");
  free(file.data);
  file = read_file("third_party/snappy/snappy-stubs-public.h.in", 1);
  sprintf(digits, "%u", major); replace(&file, "${PROJECT_VERSION_MAJOR}", digits);
  sprintf(digits, "%u", minor); replace(&file, "${PROJECT_VERSION_MINOR}", digits);
  sprintf(digits, "%u", patch); replace(&file, "${PROJECT_VERSION_PATCH}", digits);
  replace(&file, "${HAVE_SYS_UIO_H_01}", windows ? "0" : "1");
  output(out, "snappy-stubs-public.h", &file);
  file = read_file("third_party/snappy/cmake/config.h.in", 1);
  cursor = file.data;
  while ((match = strstr(cursor, "#cmakedefine01 ")) != NULL) {
    append(&result, cursor, (size_t)(match - cursor));
    end = match + strlen("#cmakedefine01 ");
    i = 0;
    while ((*end == '_' || (*end >= 'A' && *end <= 'Z') ||
            (*end >= '0' && *end <= '9')) && i < (int)sizeof(token) - 1)
      token[i++] = *end++;
    token[i] = 0;
    text(&result, "#define "); text(&result, token);
    text(&result, enabled(token, windows, arm64, msvc) ? " 1" : " 0");
    cursor = end;
  }
  text(&result, cursor); free(file.data);
  output(out, "snappy/config.h", &result);
}

static const char *environment(const char *name, const char *fallback) {
  const char *value;
  value = getenv(name);
  return value && *value ? value : fallback;
}

static void version_header(const char *out) {
  Buffer value = {0, 0, 0};
  text(&value, "#ifndef GENERATED_VERSION_H_\n#define GENERATED_VERSION_H_\n#define XE_BUILD_BRANCH ");
  quoted(&value, environment("CODEGEN_BRANCH", "tarball"));
  text(&value, "\n#define XE_BUILD_COMMIT ");
  quoted(&value, environment("CODEGEN_COMMIT", "unknown"));
  text(&value, "\n#define XE_BUILD_COMMIT_SHORT ");
  quoted(&value, environment("CODEGEN_COMMIT_SHORT", "unknown"));
  text(&value, "\n#define XE_BUILD_DATE __DATE__\n#endif\n");
  output(out, "version.h", &value);
}

static void shader_header(const char *out, const char *input,
                          const char *identifier, const char *format) {
  Buffer source;
  Buffer header = {0, 0, 0};
  const unsigned char *data;
  const char *cursor;
  size_t i;
  unsigned long word;
  char literal[16];
  if (!identifier[0] || (identifier[0] >= '0' && identifier[0] <= '9'))
    fail("invalid shader identifier", identifier);
  for (cursor = identifier; *cursor; ++cursor)
    if (!((*cursor >= 'A' && *cursor <= 'Z') ||
          (*cursor >= 'a' && *cursor <= 'z') ||
          (*cursor >= '0' && *cursor <= '9') || *cursor == '_'))
      fail("invalid shader identifier", identifier);
  source = read_file(input, 1);
  data = (const unsigned char *)source.data;
  text(&header, "/* Embedded raw shader bytecode. Generated by core-codegen. */\n");
  if (!strcmp(format, "spirv")) {
    if (source.size < 20 || source.size % 4 || data[0] != 3 ||
        data[1] != 2 || data[2] != 35 || data[3] != 7)
      fail("invalid little-endian SPIR-V module", input);
    text(&header, "const unsigned int "); text(&header, identifier);
    text(&header, "[] = {");
    for (i = 0; i < source.size; i += 4) {
      word = (unsigned long)data[i] | ((unsigned long)data[i + 1] << 8) |
        ((unsigned long)data[i + 2] << 16) | ((unsigned long)data[i + 3] << 24);
      if (!(i % 24)) text(&header, "\n ");
      sprintf(literal, "0x%08lX,", word); text(&header, literal);
    }
  } else if (!strcmp(format, "dxil")) {
    if (source.size < 32 || memcmp(data, "DXBC", 4))
      fail("invalid DXIL container", input);
    text(&header, "const unsigned char "); text(&header, identifier);
    text(&header, "[] = {"); bytes(&header, data, source.size, 0);
  } else fail("unknown shader format", format);
  text(&header, "\n};\n");
  write_file(out, &header);
  free(header.data); free(source.data);
}

static void json_string(Buffer *value, const char *string) {
  const unsigned char *cursor;
  char escape[7];
  text(value, "\"");
  for (cursor = (const unsigned char *)string; *cursor; ++cursor) {
    if (*cursor == '"' || *cursor == '\\') {
      text(value, "\\"); append(value, cursor, 1);
    } else if (*cursor < 32) {
      sprintf(escape, "\\u%04x", (unsigned int)*cursor); text(value, escape);
    } else append(value, cursor, 1);
  }
  text(value, "\"");
}

/* Clang's VFS handles SDK include spelling without changing SDK files. */
static void sdk_overlay(const char *out, const char *root) {
  static const char *directories[] = {
    "crt/include", "sdk/include/ucrt", "sdk/include/um",
    "sdk/include/shared", "sdk/include/winrt"
  };
  Files files = {NULL, 0};
  Buffer overlay = {0, 0, 0};
  char *path;
  size_t i;
  if (root[0] != '/' && !(root[0] && root[1] == ':'))
    fail("SDK path must be absolute", root);
  for (i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i)
    list_files(&files, root, directories[i], 1);
  sort_files(&files);
  text(&overlay, "{\"version\":0,\"case-sensitive\":false,\"use-external-names\":false,\"roots\":[\n");
  for (i = 0; i < files.count; ++i) {
    path = join(root, files.names[i]);
    if (i) text(&overlay, ",\n");
    text(&overlay, "{\"type\":\"file\",\"name\":"); json_string(&overlay, path);
    text(&overlay, ",\"external-contents\":"); json_string(&overlay, path);
    text(&overlay, "}"); free(path); free(files.names[i]);
  }
  text(&overlay, "\n]}\n");
  write_file(out, &overlay);
  free(files.names); free(overlay.data);
}

int main(int argc, char **argv) {
  Buffer value = {0, 0, 0};
  Buffer data;
  int windows;
  int arm64;
  int msvc;
  int i;
  const char *variable;
  const char *cursor;
  const char *start;
  if (argc < 3) fail("usage", "core-codegen inputs|signature|response|shader OUTPUT [options]");
  if (!strcmp(argv[1], "sdk-overlay")) {
    if (argc != 4) fail("usage", "core-codegen sdk-overlay OUTPUT ABSOLUTE_SDK_ROOT");
    sdk_overlay(argv[2], argv[3]);
  } else if (!strcmp(argv[1], "filelist")) {
    cursor = environment("MAKE_RESPONSE", "");
    while (*cursor) {
      while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
      if (!*cursor) break;
      if (*cursor++ != '"') fail("expected quoted response-file entry", cursor);
      start = cursor;
      while (*cursor && *cursor != '"') ++cursor;
      if (!*cursor) fail("unterminated response-file entry", start);
      append(&value, start, (size_t)(cursor - start)); text(&value, "\n");
      ++cursor;
    }
    write_file(argv[2], &value); free(value.data);
  } else if (!strcmp(argv[1], "shader")) {
    if (argc != 6) fail("usage", "core-codegen shader OUTPUT INPUT IDENTIFIER spirv|dxil");
    shader_header(argv[2], argv[3], argv[4], argv[5]);
  } else if (!strcmp(argv[1], "signature") || !strcmp(argv[1], "response")) {
    variable = !strcmp(argv[1], "response") ? "MAKE_RESPONSE" :
      (argc > 3 && !strcmp(argv[3], "host") ? "HOST_BUILD_SIGNATURE" : "MAKE_BUILD_SIGNATURE");
    text(&value, environment(variable, ""));
    if (!strcmp(argv[1], "response")) text(&value, "\n");
    write_file(argv[2], &value); free(value.data);
  } else if (!strcmp(argv[1], "inputs")) {
    windows = 0; arm64 = 0; msvc = 0;
    for (i = 3; i < argc; ++i) {
      if (!strcmp(argv[i], "windows")) windows = 1;
      else if (!strcmp(argv[i], "aarch64")) arm64 = 1;
      else if (!strcmp(argv[i], "msvc")) msvc = 1;
      else fail("unknown option", argv[i]);
    }
    configs(argv[2], windows, arm64, msvc);
    fonts(argv[2]); bundle(argv[2]); version_header(argv[2]);
    data = read_file("libretro/data/xenia_framerate_unlock.txt", 1);
    text(&value, "#include <cstddef>\nextern const unsigned char xenia_libretro_framerate_unlock[] = {");
    bytes(&value, (unsigned char *)data.data, data.size, 1);
    text(&value, "};\nextern const size_t xenia_libretro_framerate_unlock_size = ");
    number(&value, data.size); text(&value, ";\n");
    output(argv[2], "libretro_gamedb_data.cc", &value); free(data.data);
  } else fail("unknown action", argv[1]);
  return EXIT_SUCCESS;
}

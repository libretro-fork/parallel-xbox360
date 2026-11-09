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
  if (files->count > 1)
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

/* Make dependency words, including the spaces used by patch filenames. */
static void dependency_word(Buffer *out, const char *path) {
  const char *p;
  for (p = path; *p; ++p) {
    if (*p == '$') text(out, "$$");
    else {
      /* Make scans semicolons twice: recipe splitting, then word parsing. */
      if (*p == ';') text(out, "\\\\");
      if (*p == ' ' || *p == '\t' || *p == '#' || *p == ':' ||
          *p == ';' || *p == '|' || *p == '\\')
        text(out, "\\");
      append(out, p, 1);
    }
  }
}

static void bundle(const char *out) {
  const char *root = "libretro/data/patches";
  Files files = {NULL, 0};
  Buffer raw = {0, 0, 0};
  Buffer file;
  Buffer source = {0, 0, 0};
  Buffer header = {0, 0, 0};
  Buffer deps = {0, 0, 0};
  unsigned char *compressed;
  int compressed_size;
  size_t i;
  char *path;
  list_files(&files, root, "", 1);
  sort_files(&files);
  path = join(out, "embedded_bundle_patches.cc");
  dependency_word(&deps, path); free(path);
  text(&deps, ": "); dependency_word(&deps, root);
  u32(&raw, files.count);
  for (i = 0; i < files.count; ++i) {
    path = join(root, files.names[i]);
    text(&deps, " "); dependency_word(&deps, path);
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
  text(&header, "#pragma once\n#include <cstddef>\nnamespace xe::embedded_bundle_patches {\nextern const unsigned char kBundleData[];\nextern const size_t kBundleSize;\nextern const size_t kBundleRawSize;\n}\n");
  text(&source, "#include \"embedded_bundle_patches.h\"\nnamespace xe::embedded_bundle_patches {\nconst unsigned char kBundleData[] = {");
  bytes(&source, compressed, (size_t)compressed_size, 0);
  text(&source, "};\nconst size_t kBundleSize = sizeof(kBundleData);\nconst size_t kBundleRawSize = ");
  number(&source, raw.size);
  text(&source, ";\n}\n");
  output(out, "embedded_bundle_patches.h", &header);
  output(out, "embedded_bundle_patches.cc", &source);
  text(&deps, "\n");
  /* As with compiler -MP depfiles, removed inputs must not break Make before
   * the directory dependency can regenerate this list. */
  for (i = 0; i < files.count; ++i) {
    path = join(root, files.names[i]);
    dependency_word(&deps, path); free(path);
    text(&deps, ":\n");
  }
  output(out, "embedded_bundle_patches.d", &deps);
  free(compressed);
  free(raw.data);
  free_files(&files);
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
    for (i = 3; i < argc; ++i) {
      if (strcmp(argv[i], "windows") && strcmp(argv[i], "aarch64") &&
          strcmp(argv[i], "msvc")) fail("unknown option", argv[i]);
    }
    bundle(argv[2]); version_header(argv[2]);
    data = read_file("libretro/data/xenia_framerate_unlock.txt", 1);
    text(&value, "#include <cstddef>\nextern const unsigned char xenia_libretro_framerate_unlock[] = {");
    bytes(&value, (unsigned char *)data.data, data.size, 1);
    text(&value, "};\nextern const size_t xenia_libretro_framerate_unlock_size = ");
    number(&value, data.size); text(&value, ";\n");
    output(argv[2], "libretro_gamedb_data.cc", &value); free(data.data);
  } else fail("unknown action", argv[1]);
  return EXIT_SUCCESS;
}

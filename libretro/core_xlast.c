#include "core_xlast.h"
#include "core_decompress.h"
#include <formats/rxml.h>
#include <stdlib.h>
#include <string.h>

struct xe_xlast {
  rxml_document_t *tree;
  rxml_node_t *project, *product, *localized, *presence, *properties, *contexts, *queries;
  uint32_t *string_keys;
  rxml_node_t **string_nodes;
  size_t string_mask;
};
static const char *const locales[13] = {
  "", "en-US", "ja-JP", "de-DE", "fr-FR", "es-ES", "it-IT", "ko-KR",
  "zh-CHT", "pt-PT", "", "pl-PL", "ru-RU"
};
static const char *const products[7] = {
  "offlinePlayersMax", "systemLinkPlayersMax", "livePlayersMax", "publisherStringId",
  "developerStringId", "sellTextStringId", "genreTextStringId"
};
static rxml_node_t *child(const rxml_node_t *node, const char *name) {
  rxml_node_t *p;
  for (p = node ? node->children : NULL; p; p = p->next)
    if (p->name && !strcmp(p->name, name)) return p;
  return NULL;
}
static const char *attribute(const rxml_node_t *node, const char *name) {
  const struct rxml_attrib_node *p;
  for (p = node ? node->attrib : NULL; p; p = p->next)
    if (!strcmp(p->attrib, name)) return p->value ? p->value : "";
  return "";
}
static rxml_node_t *match(const rxml_node_t *parent, const char *name,
    const char *attr, const char *value) {
  rxml_node_t *p;
  for (p = parent ? parent->children : NULL; p; p = p->next)
    if ((!name || !strcmp(p->name, name)) && !strcmp(attribute(p, attr), value)) return p;
  return NULL;
}
static char *decimal(char buffer[11], uint32_t value) {
  char *p = buffer + 10;
  *p = 0;
  do { *--p = (char)('0' + value % 10); value /= 10; } while (value);
  return p;
}
static void hexadecimal(char buffer[11], uint32_t value) {
  static const char digits[] = "0123456789ABCDEF";
  unsigned i;
  buffer[0] = '0'; buffer[1] = 'x'; buffer[10] = 0;
  for (i = 0; i < 8; ++i) buffer[9 - i] = digits[(value >> (i * 4)) & 15];
}
/* Match the old decimal/forced-hex prefix parsing, without throwing or
 * asserting on malformed title metadata. Overflow and missing values are 0. */
static uint32_t number(const char *p, int hex) {
  uint32_t value = 0;
  unsigned base, digit, any = 0;
  int negative = 0;
  size_t length;
  if (*p == '-') { negative = 1; ++p; }
  length = strlen(p);
  if (length >= 2 && p[0] == '0' && p[1] == 'x') { hex = 1; p += 2; length -= 2; }
  if (length && p[length - 1] == 'h') hex = 1;
  base = hex ? 16 : 10;
  while (*p) {
    if (*p >= '0' && *p <= '9') digit = (unsigned)(*p - '0');
    else if (*p >= 'a' && *p <= 'f') digit = (unsigned)(*p - 'a') + 10;
    else if (*p >= 'A' && *p <= 'F') digit = (unsigned)(*p - 'A') + 10;
    else break;
    if (digit >= base) break;
    if (value > (0xffffffffU - digit) / base) return 0;
    value = value * base + digit; any = 1; ++p;
  }
  if (!any) return 0;
  return negative ? 0U - value : value;
}
static rxml_node_t *id_node(const rxml_node_t *parent, const char *name,
    const char *attr, uint32_t id, int hex) {
  char buffer[11];
  if (hex) { hexadecimal(buffer, id); return match(parent, name, attr, buffer); }
  return match(parent, name, attr, decimal(buffer, id));
}
static int string_id(rxml_node_t *node, uint32_t *value) {
  if (!node || !value) return 0;
  *value = number(attribute(node, "stringId"), 0); return 1;
}
void xe_xlast_destroy(xe_xlast *doc) {
  if (doc) { free(doc->string_keys); rxml_free_document(doc->tree); free(doc); }
}
const char *xe_xlast_title(const xe_xlast *doc) {
  return attribute(doc ? doc->project : NULL, "titleName");
}
unsigned xe_xlast_product(const xe_xlast *doc, uint32_t values[7]) {
  const struct rxml_attrib_node *p;
  unsigned mask = 0, i;
  if (!doc || !values) return 0;
  for (p = doc->product ? doc->product->attrib : NULL; p; p = p->next)
    for (i = 0; i < 7; ++i)
      if (!(mask & (1U << i)) && !strcmp(p->attrib, products[i]) && p->value && *p->value) {
        values[i] = number(p->value, 0); mask |= 1U << i; break;
      }
  return mask;
}
size_t xe_xlast_languages(const xe_xlast *doc, xe_xlast_sink sink, void *opaque) {
  rxml_node_t *p;
  unsigned i;
  size_t count = 0;
  for (p = doc && doc->localized ? doc->localized->children : NULL; p; p = p->next)
    if (!strcmp(p->name, "SupportedLocale"))
      for (i = 1; i < 13; ++i)
        if (*locales[i] && !strcmp(attribute(p, "locale"), locales[i])) {
          if (sink) sink(opaque, i);
          ++count; break;
        }
  return count;
}
static size_t id_slot(uint32_t value, size_t mask) {
  value ^= value >> 16; value *= 0x7feb352dU; value ^= value >> 15;
  return (size_t)value & mask;
}
/* Build once, privately, before publishing the immutable document. Keys must
 * have exactly the decimal spelling that the former XPath query requested.
 * Dense arrays avoid per-entry allocations and retain first-duplicate wins. */
static void index_strings(xe_xlast *doc) {
  rxml_node_t *p;
  size_t count = 0, cap = 128, mask, slot, probe;
  uint32_t key;
  char buffer[11];
  const char *text;
  for (p = doc->localized ? doc->localized->children : NULL; p; p = p->next)
    if (!strcmp(p->name, "LocalizedString")) ++count;
  if (count < 64 || count > (size_t)-1 / 2) return;
  while (cap - cap / 3 < count) {
    if (cap > (size_t)-1 / 2) return;
    cap *= 2;
  }
  if (cap > (size_t)-1 / (sizeof(uint32_t) + sizeof(rxml_node_t *))) return;
  doc->string_keys = (uint32_t *)malloc(cap * (sizeof(uint32_t) + sizeof(rxml_node_t *)));
  if (!doc->string_keys) return;
  doc->string_nodes = (rxml_node_t **)(void *)(doc->string_keys + cap);
  memset(doc->string_nodes, 0, cap * sizeof(rxml_node_t *));
  mask = cap - 1;
  for (p = doc->localized->children; p; p = p->next) {
    if (strcmp(p->name, "LocalizedString")) continue;
    text = attribute(p, "id"); key = number(text, 0);
    if (strcmp(text, decimal(buffer, key))) continue;
    slot = id_slot(key, mask); probe = 0;
    while (doc->string_nodes[slot] && doc->string_keys[slot] != key) {
      slot = (slot + 1) & mask;
      /* Adversarial collisions must not make construction quadratic. */
      if (++probe > 32) {
        free(doc->string_keys); doc->string_keys = NULL; doc->string_nodes = NULL;
        return;
      }
    }
    if (!doc->string_nodes[slot]) {
      doc->string_keys[slot] = key; doc->string_nodes[slot] = p;
    }
  }
  doc->string_mask = mask;
}
static rxml_node_t *localized_node(const xe_xlast *doc, uint32_t id) {
  size_t slot;
  if (!doc->string_nodes) return id_node(doc->localized, "LocalizedString", "id", id, 0);
  slot = id_slot(id, doc->string_mask);
  while (doc->string_nodes[slot]) {
    if (doc->string_keys[slot] == id) return doc->string_nodes[slot];
    slot = (slot + 1) & doc->string_mask;
  }
  return NULL;
}
const char *xe_xlast_localized(const xe_xlast *doc, uint32_t id, uint32_t language) {
  rxml_node_t *p;
  const char *locale = language < 13 && *locales[language] ? locales[language] : locales[1];
  if (!doc) return "";
  p = localized_node(doc, id);
  p = match(p, NULL, "locale", locale);
  return p && p->data ? p->data : "";
}
int xe_xlast_presence(const xe_xlast *doc, uint32_t id, uint32_t *value) {
  return string_id(doc ? id_node(doc->presence, "PresenceMode", "contextValue", id, 0) : NULL, value);
}
int xe_xlast_property(const xe_xlast *doc, uint32_t id, uint32_t *value) {
  return string_id(doc ? id_node(doc->properties, "Property", "id", id, 1) : NULL, value);
}
int xe_xlast_context(const xe_xlast *doc, uint32_t id, uint32_t context, uint32_t *value) {
  rxml_node_t *p = doc ? id_node(doc->contexts, "Context", "id", id, 1) : NULL;
  return string_id(id_node(p, "ContextValue", "value", context, 0), value);
}
const xe_xlast_query *xe_xlast_find_query(const xe_xlast *doc, uint32_t id) {
  return doc ? id_node(doc->queries, "Query", "id", id, 0) : NULL;
}
const char *xe_xlast_query_name(const xe_xlast_query *query) {
  return attribute(query, "friendlyName");
}
size_t xe_xlast_query_values(const xe_xlast_query *query, unsigned kind,
    xe_xlast_sink sink, void *opaque) {
  static const char *const names[3] = {"Returns", "Parameters", "Filters"};
  rxml_node_t *p;
  size_t count = 0;
  if (kind > 2) return 0;
  p = child(query, names[kind]);
  for (p = p ? p->children : NULL; p; p = p->next) {
    if (sink) sink(opaque, number(attribute(p, kind == 2 ? "left" : "id"), 1));
    ++count;
  }
  return count;
}

static unsigned unit_at(const uint8_t *input, unsigned width, int little) {
  unsigned value = 0, i;
  for (i = 0; i < width; ++i)
    value |= (unsigned)input[i] << (8 * (little ? i : width - 1 - i));
  return value;
}
static int codepoint(const uint8_t *input, size_t size, size_t *at,
    unsigned width, int little, unsigned *value) {
  unsigned ch, low;
  if (size - *at < width) return 0;
  ch = unit_at(input + *at, width, little); *at += width;
  if (width == 2 && ch >= 0xd800 && ch <= 0xdbff) {
    if (size - *at < 2) return 0;
    low = unit_at(input + *at, 2, little); *at += 2;
    if (low < 0xdc00 || low > 0xdfff) return 0;
    ch = 0x10000 + ((ch - 0xd800) << 10) + low - 0xdc00;
  } else if (ch >= 0xd800 && ch <= 0xdfff) return 0;
  if (ch > 0x10ffff) return 0;
  *value = ch; return 1;
}
/* Fuse the existing interior-NUL scan with UTF-8 validation. ASCII spans
 * advance eight bytes at once through alias/alignment-safe loads. */
static int valid_utf8(const uint8_t *input, size_t size) {
  const uint64_t high = ((uint64_t)0x80808080U << 32) | 0x80808080U;
  const uint64_t ones = ((uint64_t)0x01010101U << 32) | 0x01010101U;
  size_t at = 0;
  uint64_t word;
  unsigned ch, n, i, low;
  while (at < size) {
    if (size - at >= 8) {
      memcpy(&word, input + at, 8);
      if (!(word & high) && !((word - ones) & ~word & high)) { at += 8; continue; }
    }
    ch = input[at++];
    if (!ch) return 0;
    if (ch < 0x80) continue;
    if (ch >= 0xc2 && ch <= 0xdf) { n = 1; low = 0x80; ch &= 31; }
    else if (ch >= 0xe0 && ch <= 0xef) { n = 2; low = 0x800; ch &= 15; }
    else if (ch >= 0xf0 && ch <= 0xf4) { n = 3; low = 0x10000; ch &= 7; }
    else return 0;
    if (size - at < n) return 0;
    for (i = 0; i < n; ++i) {
      unsigned byte = input[at++];
      if ((byte & 0xc0) != 0x80) return 0;
      ch = (ch << 6) | (byte & 63);
    }
    if (ch < low || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff)) return 0;
  }
  return 1;
}
/* Only an actual encoding conversion needs a second input representation.
 * Ordinary UTF-8, including its BOM, stays in the caller's allocation. */
static char *utf8_owned(char *input, size_t *size) {
  const uint8_t *bytes = (const uint8_t *)input;
  size_t n = *size, offset = 0, at, total = 0, write = 0;
  unsigned width = 1, little = 0, ch, added;
  char *out;
  if (n >= 4 && bytes[0] == 0xff && bytes[1] == 0xfe && !bytes[2] && !bytes[3]) {
    width = 4; little = 1; offset = 4;
  } else if (n >= 4 && !bytes[0] && !bytes[1] && bytes[2] == 0xfe && bytes[3] == 0xff) {
    width = 4; offset = 4;
  } else if (n >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
    width = 2; little = 1; offset = 2;
  } else if (n >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
    width = 2; offset = 2;
  } else if (n >= 4 && bytes[0] == '<' && !bytes[1] && !bytes[2] && !bytes[3]) {
    width = 4; little = 1;
  } else if (n >= 4 && !bytes[0] && !bytes[1] && !bytes[2] && bytes[3] == '<') {
    width = 4;
  } else if (n >= 4 && bytes[0] == '<' && !bytes[1] && !bytes[3]) {
    width = 2; little = 1;
  } else if (n >= 4 && !bytes[0] && bytes[1] == '<' && !bytes[2]) width = 2;
  if (width == 1) {
    while (n && !input[n - 1]) --n;
    if (!valid_utf8((const uint8_t *)input, n)) { free(input); return NULL; }
    input[n] = 0; *size = n; return input;
  }
  at = offset;
  while (at < n) {
    if (!codepoint(bytes, n, &at, width, little, &ch)) { free(input); return NULL; }
    if (!ch) {
      while (at < n) {
        if (!codepoint(bytes, n, &at, width, little, &ch) || ch) { free(input); return NULL; }
      }
      break;
    }
    added = ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
    if (total > (size_t)-1 - added - 1) { free(input); return NULL; }
    total += added;
  }
  out = (char *)malloc(total + 1);
  if (!out) { free(input); return NULL; }
  at = offset;
  while (write < total) {
    if (!codepoint(bytes, n, &at, width, little, &ch)) { free(out); free(input); return NULL; }
    if (ch < 0x80) out[write++] = (char)ch;
    else if (ch < 0x800) {
      out[write++] = (char)(0xc0 | (ch >> 6)); out[write++] = (char)(0x80 | (ch & 63));
    } else if (ch < 0x10000) {
      out[write++] = (char)(0xe0 | (ch >> 12)); out[write++] = (char)(0x80 | ((ch >> 6) & 63));
      out[write++] = (char)(0x80 | (ch & 63));
    } else {
      out[write++] = (char)(0xf0 | (ch >> 18)); out[write++] = (char)(0x80 | ((ch >> 12) & 63));
      out[write++] = (char)(0x80 | ((ch >> 6) & 63)); out[write++] = (char)(0x80 | (ch & 63));
    }
  }
  out[write] = 0; free(input); *size = write; return out;
}
xe_xlast *xe_xlast_parse_owned(char *input, size_t len) {
  xe_xlast *doc;
  rxml_node_t *root;
  if (!input || len == (size_t)-1) { free(input); return NULL; }
  input = utf8_owned(input, &len);
  if (!input) return NULL;
  doc = (xe_xlast *)calloc(1, sizeof(*doc));
  if (!doc) { free(input); return NULL; }
  doc->tree = rxml_load_document_owned_opts(input, len,
      RXML_OPT_STRICT_EOF | RXML_OPT_XML10 | RXML_OPT_FIRST_TEXT, NULL);
  if (!doc->tree) { free(doc); return NULL; }
  root = rxml_root_node(doc->tree);
  if (root && root->name && !strcmp(root->name, "XboxLiveSubmissionProject"))
    doc->project = child(root, "GameConfigProject");
  doc->product = child(doc->project, "ProductInformation");
  doc->localized = child(doc->project, "LocalizedStrings");
  doc->presence = child(doc->project, "Presence");
  doc->properties = child(doc->project, "Properties");
  doc->contexts = child(doc->project, "Contexts");
  doc->queries = child(child(doc->project, "Matchmaking"), "Queries");
  index_strings(doc);
  return doc;
}
xe_xlast *xe_xlast_create_gzip(const uint8_t *input, size_t size, size_t decoded) {
  char *output;
  if (!input || !size || !decoded || decoded == (size_t)-1) return NULL;
  output = (char *)malloc(decoded + 1);
  if (!output) return NULL;
  if (!xe_decompress_exact(output, decoded, input, size, 31)) { free(output); return NULL; }
  output[decoded] = 0;
  return xe_xlast_parse_owned(output, decoded);
}

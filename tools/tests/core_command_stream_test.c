#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static size_t calls, live, fail_at;
static void *test_alloc(size_t size) {
  void *p;
  ++calls; if (fail_at == calls) return NULL;
  p = malloc(size); if (p) { ++live; memset(p, 0xa5, size); } return p;
}
static void test_free(void *p) { if (p) --live; free(p); }
#define XE_COMMAND_STREAM_ALLOC test_alloc
#define XE_COMMAND_STREAM_FREE test_free
#include "core_command_stream.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct test_header { uint32_t command, elements; } test_header;
static size_t arguments_for(uint32_t command) { return (command * 37u) % 129u; }
static unsigned char payload_for(uint32_t command, size_t byte) { return (unsigned char)(command + byte * 13u); }
static int record(xe_command_stream *s, uint32_t command) {
  uint32_t elements;
  size_t packet_size, i, arguments = arguments_for(command);
  unsigned char *packet;
  test_header *header;
  CHECK(xe_command_packet_layout(arguments, sizeof(*header), &elements, &packet_size));
  packet = (unsigned char *)xe_command_stream_append(s, packet_size);
  CHECK(packet != NULL && (uintptr_t)packet % sizeof(uintmax_t) == 0);
  header = (test_header *)packet; header->command = command; header->elements = elements;
  for (i = 0; i < arguments; ++i) packet[sizeof(*header) + i] = payload_for(command, i);
  return 0;
}
static int replay(const xe_command_stream *s, uint32_t count) {
  const xe_command_page *page;
  uint32_t expected = 0;
  size_t total = 0;
  for (page = s->first; page; page = page->next) {
    size_t offset = 0;
    CHECK(page->used <= page->capacity && page->used % sizeof(uintmax_t) == 0);
    while (offset < page->used) {
      const unsigned char *packet = xe_command_page_const_data(page) + offset;
      const test_header *header = (const test_header *)packet;
      size_t arguments, bytes, i;
      uint32_t elements;
      CHECK(page->used - offset >= sizeof(*header) && header->command == expected);
      arguments = arguments_for(expected);
      CHECK(xe_command_packet_layout(arguments, sizeof(*header), &elements, &bytes));
      CHECK(header->elements == elements && bytes <= page->used - offset);
      for (i = 0; i < arguments; ++i) CHECK(packet[sizeof(*header) + i] == payload_for(expected, i));
      offset += bytes; ++expected;
    }
    total += offset;
  }
  CHECK(total == s->size && expected == count); return 0;
}
static int reserved_record(xe_command_stream *s,uint32_t command,int publish) {
  xe_command_reservation lease = {0};
  uint32_t elements;size_t bytes,i,arguments=arguments_for(command);
  unsigned char *packet;test_header *header;
  if(!xe_command_packet_layout(arguments,sizeof(test_header),&elements,&bytes))return -1;
  packet=(unsigned char*)xe_command_stream_reserve(s,bytes,&lease);if(!packet)return -1;
  header=(test_header*)packet;header->command=command;header->elements=elements;
  for(i=0;i<arguments;++i)packet[sizeof(*header)+i]=payload_for(command,i);
  if(!publish)return 0;
  return xe_command_stream_commit(s,&lease)?1:-1;
}
static int reservations(void) {
  xe_command_stream s={0},other={0};xe_command_reservation lease={0};
  xe_command_page *last,*current;size_t size,used,before;unsigned i;
  unsigned char *borrowed;
  CHECK(!xe_command_stream_reserve(&s,8,&lease));
  CHECK(!xe_command_stream_commit(&s,&lease));
  CHECK(xe_command_stream_init(&s,64) && xe_command_stream_init(&other,64));
  CHECK(record(&s,0)==0);borrowed=xe_command_page_data(s.first);
  size=s.size;current=s.current;used=current->used;
  CHECK(xe_command_stream_reserve(&s,256,&lease));
  CHECK(s.size==size && s.current==current && current->used==used && replay(&s,1)==0);
  CHECK(!xe_command_stream_commit(&other,&lease));
  CHECK(reserved_record(&s,1,0)==0 && s.size==size && replay(&s,1)==0);
  CHECK(reserved_record(&s,1,1)==1 && replay(&s,2)==0);
  CHECK(reserved_record(&s,2,1)==1 && replay(&s,3)==0 && ((test_header*)borrowed)->command==0);
  last=s.last;current=s.current;size=s.size;used=current->used;
  fail_at=calls+1;CHECK(!xe_command_stream_reserve(&s,1024,&lease));fail_at=0;
  CHECK(s.last==last && s.current==current && s.size==size && current->used==used && replay(&s,3)==0);
  CHECK(!xe_command_stream_reserve(&s,0,&lease) && !xe_command_stream_reserve(&s,3,&lease));
  xe_command_stream_reset(&s);
  CHECK(xe_command_stream_reserve(&s,16,&lease));
  CHECK(record(&s,0)==0 && !xe_command_stream_commit(&s,&lease) && replay(&s,1)==0);
  xe_command_stream_reset(&s);
  CHECK(xe_command_stream_reserve(&s,8,&lease));
  {test_header *header=(test_header*)(xe_command_page_data(lease.page)+lease.offset);
   header->command=0;header->elements=0;}
  CHECK(xe_command_stream_commit(&s,&lease) && !xe_command_stream_commit(&s,&lease) && replay(&s,1)==0);
  xe_command_stream_reset(&s);
  for(i=0;i<20000;++i){if(!(i&63))CHECK(reserved_record(&s,i,0)==0);CHECK(reserved_record(&s,i,1)==1);}
  CHECK(replay(&s,20000)==0);before=calls;
  xe_command_stream_reset(&s);
  for(i=0;i<20000;++i){if(!(i&63))CHECK(reserved_record(&s,i,0)==0);CHECK(reserved_record(&s,i,1)==1);}
  CHECK(replay(&s,20000)==0 && calls==before);
  xe_command_stream_destroy(&s);xe_command_stream_destroy(&other);CHECK(!live);
  puts("Command reservations: 40,000 published records, abandoned payloads, foreign/stale/double commits and allocation rollback pass");
  return 0;
}
int main(void) {
  xe_command_stream s = {0};
  xe_command_page *first, *last, *current;
  unsigned char *borrowed;
  uint32_t elements, i;
  size_t bytes, before, old_size, old_used;
  CHECK(!xe_command_stream_append(&s, 8));
  CHECK(!xe_command_stream_init(&s, 0));
  CHECK(!xe_command_stream_init(&s, (size_t)-1) && !calls);
  for (i = 0; i < 65536; ++i) {
    CHECK(xe_command_packet_layout(i, 8, &elements, &bytes));
    CHECK(elements == (i + 7u) / 8u && bytes == 8u + ((i + 7u) / 8u) * 8u);
  }
  CHECK(!xe_command_packet_layout((size_t)-1, 8, &elements, &bytes));
  CHECK(!xe_command_packet_layout(8, (size_t)-1 - 7, &elements, &bytes));
  CHECK(!xe_command_packet_layout(1, 3, &elements, &bytes));
#if SIZE_MAX > UINT32_MAX
  CHECK(!xe_command_packet_layout(((size_t)UINT32_MAX + 1) * sizeof(uintmax_t), 8, &elements, &bytes));
#endif
  CHECK(xe_command_packet_layout(0, 8, &elements, &bytes) && !elements && bytes == 8);
  fail_at = calls + 1; CHECK(!xe_command_stream_init(&s, 64) && !s.first && !live);
  fail_at = 0; CHECK(xe_command_stream_init(&s, 64));
  CHECK(!xe_command_stream_init(&s, 64));
  borrowed = (unsigned char *)xe_command_stream_append(&s, 64);
  CHECK(borrowed && borrowed[0] == 0xa5 && borrowed[63] == 0xa5);
  memset(borrowed, 0x31, 64);
  first = s.first; last = s.last; current = s.current; old_size = s.size; old_used = current->used;
  fail_at = calls + 1;
  CHECK(!xe_command_stream_append(&s, 128));
  CHECK(s.first == first && s.last == last && s.current == current && s.size == old_size && current->used == old_used);
  fail_at = 0;
  CHECK(xe_command_stream_append(&s, 128) && s.first == first && borrowed[0] == 0x31 && borrowed[63] == 0x31);
  CHECK(!xe_command_stream_append(&s, 0) && !xe_command_stream_append(&s, 3));
  CHECK(!xe_command_stream_append(&s, (size_t)-1 - 7));
  xe_command_stream_reset(&s); CHECK(!s.size && s.current == s.first && borrowed[0] == 0x31);
  /* Varied packet sizes skip undersized retained pages and preserve wire order. */
  for (i = 0; i < 20000; ++i) CHECK(record(&s, i) == 0);
  CHECK(replay(&s, 20000) == 0);
  before = calls;
  xe_command_stream_reset(&s);
  for (i = 0; i < 20000; ++i) CHECK(record(&s, i) == 0);
  CHECK(replay(&s, 20000) == 0 && calls == before);
  xe_command_stream_destroy(&s); xe_command_stream_destroy(&s); CHECK(!live);
  /* Exceed the former native 8 MiB bound without moving the first view. */
  CHECK(xe_command_stream_init(&s, 8u * 1024u * 1024u));
  borrowed = (unsigned char *)xe_command_stream_append(&s, 8u * 1024u * 1024u);
  CHECK(borrowed); borrowed[0] = 0x71;
  CHECK(xe_command_stream_append(&s, 32) && borrowed[0] == 0x71 && s.size > 8u * 1024u * 1024u);
  xe_command_stream_destroy(&s); CHECK(!live);
  CHECK(reservations()==0);
  puts("Command stream C89: aligned varied packets, 40,000 exact replay records, retained reset, stable views beyond 8 MiB, bounds and allocation rollback pass");
  return 0;
}

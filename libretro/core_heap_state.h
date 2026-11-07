/* Borrowed, bounded heap restore plans; algorithms are MSVC C89. */
#ifndef XE_CORE_HEAP_STATE_H
#define XE_CORE_HEAP_STATE_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
typedef struct xe_heap_page { uint32_t base,count,state,reserved; } xe_heap_page;
typedef void (*xe_heap_decode)(const void *,xe_heap_page *);
typedef struct xe_heap_state {
  const unsigned char *entries,*data;
  size_t pages,stride,page_bytes,data_bytes;
  uint32_t unreserved;
  int contents;
  xe_heap_decode decode;
} xe_heap_state;
typedef struct xe_heap_run { uint32_t first,count; size_t offset,bytes; } xe_heap_run;
typedef struct xe_heap_scan { size_t page,offset; } xe_heap_scan;
static INLINE int xe_heap_state_plan(xe_heap_state *out,const void *entries,
    size_t pages,size_t stride,size_t page_bytes,int contents,xe_heap_decode decode) {
  xe_heap_state plan;
  xe_heap_page page;
  size_t i;
  if(!out || !decode || !stride || !page_bytes || page_bytes>UINT32_MAX ||
      pages>UINT32_MAX || pages>SIZE_MAX/stride || (pages && !entries)) return 0;
  plan.entries=(const unsigned char*)entries;plan.data=NULL;
  plan.pages=pages;plan.stride=stride;plan.page_bytes=page_bytes;
  plan.contents=contents!=0;plan.data_bytes=0;plan.unreserved=0;plan.decode=decode;
  for(i=0;i<pages;++i) {
    decode(plan.entries+i*stride,&page);
    if(page.reserved || page.state>3 || (page.state && (!page.count ||
        page.base>i || page.base>=pages || page.count>pages-page.base ||
        i-page.base>=page.count))) return 0;
    if(!page.state) ++plan.unreserved;
    if(plan.contents && (page.state&2)) {
      if(page_bytes>SIZE_MAX-plan.data_bytes) return 0;
      plan.data_bytes+=page_bytes;
    }
  }
  *out=plan;return 1;
}
/* Only used after planning; entries remain immutable through replay. Native
 * commit byte counts are uint32, so even huge runs are split before overflow. */
static INLINE int xe_heap_state_next(const xe_heap_state *view,xe_heap_scan *scan,
    xe_heap_run *out) {
  xe_heap_page page;
  size_t first,count,limit;
  if(!view || !scan || !out || !view->contents || scan->page>=view->pages) return 0;
  while(scan->page<view->pages) {
    view->decode(view->entries+scan->page*view->stride,&page);
    if(page.state&2) break;
    ++scan->page;
  }
  if(scan->page==view->pages) return 0;
  first=scan->page;count=0;limit=UINT32_MAX/view->page_bytes;
  do {
    ++scan->page;++count;
    if(scan->page==view->pages || count==limit) break;
    view->decode(view->entries+scan->page*view->stride,&page);
  } while(page.state&2);
  out->first=(uint32_t)first;out->count=(uint32_t)count;
  out->offset=scan->offset;out->bytes=count*view->page_bytes;
  scan->offset+=out->bytes;return 1;
}
#endif

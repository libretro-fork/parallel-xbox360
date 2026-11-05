#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t allocations, frees;
static int fail;
static void *walk_alloc(size_t bytes) { ++allocations; return fail ? NULL : malloc(bytes); }
static void walk_free(void *value) { if (value) ++frees; free(value); }
#define XE_SPV_WALK_ALLOC walk_alloc
#define XE_SPV_WALK_FREE walk_free
#include "../../libretro/core_spv_walk.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct node {
   size_t index, count;
   struct node *successors[2], *merge, *continuation;
} node;
typedef struct context {
   size_t count, ids[1024], headers[1024];
   int reasons[1024], fail_after;
} context;
static size_t index_of(void *opaque, const void *value) { (void)opaque; return ((const node *)value)->index; }
static size_t successors(const void *value) { return ((const node *)value)->count; }
static const void *successor(const void *value, size_t i) { return ((const node *)value)->successors[i]; }
static int targets(const void *value, const void **merge, const void **continuation)
{
   const node *n = (const node *)value;
   *merge = n->merge; *continuation = n->continuation;
   return 1;
}
static int visit(void *opaque, const void *value, int reason, const void *header)
{
   context *c = (context *)opaque;
   if (c->fail_after >= 0 && c->count == (size_t)c->fail_after) return 0;
   if (c->count >= 1024) return 0;
   c->ids[c->count] = ((const node *)value)->index;
   c->reasons[c->count] = reason;
   c->headers[c->count] = header ? ((const node *)header)->index : (size_t)-1;
   ++c->count;
   return 1;
}
int main(void)
{
   node nodes[1024];
   context c;
   xe_spv_walk_ops ops = {index_of, successors, successor, targets, visit};
   size_t i, round, before;
   memset(nodes, 0, sizeof(nodes));
   for (i = 0; i < 1024; ++i) nodes[i].index = i;
   nodes[0].merge = &nodes[3]; nodes[0].continuation = &nodes[2];
   nodes[0].count = 1; nodes[0].successors[0] = &nodes[1];
   nodes[2].count = 1; nodes[2].successors[0] = &nodes[0];
   nodes[3].count = 1; nodes[3].successors[0] = &nodes[4];
   for (round = 0; round < 10000; ++round) {
      int live = (int)(round & 1);
      c.count = 0; c.fail_after = -1;
      nodes[1].count = live ? 2 : 1;
      nodes[1].successors[0] = live ? &nodes[2] : &nodes[0];
      nodes[1].successors[1] = &nodes[3];
      CHECK(xe_spv_walk_graph(&ops, &c, nodes, 5));
      CHECK(c.count == (size_t)(live ? 5 : 4));
      for (i = 0; i < c.count; ++i) CHECK(c.ids[i] == i);
      CHECK(c.reasons[2] == (live ? XE_SPV_WALK_CONTROL : XE_SPV_WALK_DEAD_CONTINUE));
      CHECK(c.reasons[3] == (live ? XE_SPV_WALK_CONTROL : XE_SPV_WALK_DEAD_MERGE));
      CHECK(c.headers[2] == 0 && c.headers[3] == 0);
      CHECK(!allocations);
   }
   memset(nodes, 0, sizeof(nodes));
   for (i = 0; i < 1024; ++i) {
      nodes[i].index = i;
      if (i != 1023) { nodes[i].count = 1; nodes[i].successors[0] = &nodes[i + 1]; }
   }
   c.count = 0; c.fail_after = -1;
   CHECK(xe_spv_walk_graph(&ops, &c, nodes, 1024));
   CHECK(c.count == 1024 && allocations == 1 && frees == 1);
   for (i = 0; i < c.count; ++i) CHECK(c.ids[i] == i && c.reasons[i] == XE_SPV_WALK_CONTROL);
   c.count = 0; c.fail_after = 2;
   CHECK(!xe_spv_walk_graph(&ops, &c, nodes, 1024));
   CHECK(c.count == 2 && allocations == frees);
   before = frees; fail = 1; c.count = 0;
   CHECK(!xe_spv_walk_graph(&ops, &c, nodes, 1024) && !c.count && frees == before);
   fail = 0; c.fail_after = -1;
   nodes[0].index = 1024;
   before = frees;
   CHECK(!xe_spv_walk_graph(&ops, &c, nodes, 1024) && frees == before + 1);
   CHECK(!xe_spv_walk_graph(&ops, &c, NULL, 0));
   puts("PASS: 10000 C89 loop orders/reasons without allocations, 1024-node chain, callback/allocator/bounds failures");
   return 0;
}

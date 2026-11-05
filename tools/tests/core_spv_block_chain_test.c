#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libretro/core_spv_block_chain.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static xe_spv_block_link *expected;
static size_t destroyed;
static int mismatch;
static void visit(xe_spv_block_link *node)
{
   if (node != expected) mismatch = 1;
   expected = node->next;
   node->next = NULL; /* Caller reuses stack-backed payloads only in this test. */
   ++destroyed;
}
static void release(xe_spv_block_link *node) { ++destroyed; free(node); }
int main(void)
{
   xe_spv_block_chain chain = {0};
   xe_spv_block_link nodes[1024], *node;
   size_t round, i;
   memset(nodes, 0, sizeof(nodes));
   xe_spv_block_chain_destroy(&chain, visit);
   CHECK(!xe_spv_block_chain_append(&chain, NULL));
   for (round = 0; round < 10000; ++round) {
      for (i = 0; i < 1024; ++i) {
         CHECK(xe_spv_block_chain_append(&chain, &nodes[i]));
         CHECK(chain.count == i + 1 && chain.head == nodes && chain.tail == &nodes[i]);
         CHECK(!xe_spv_block_chain_append(&chain, &nodes[i]));
      }
      CHECK(!xe_spv_block_chain_append(&chain, nodes));
      expected = nodes;
      xe_spv_block_chain_destroy(&chain, visit);
      CHECK(!mismatch && !expected && destroyed == (round + 1) * 1024);
      memset(&chain, 0, sizeof(chain));
   }
   for (i = 0; i < 3; ++i) {
      node = (xe_spv_block_link *)calloc(1, sizeof(*node));
      CHECK(node && xe_spv_block_chain_append(&chain, node));
   }
   xe_spv_block_chain_destroy(&chain, release); /* ASan checks next-before-free. */
   CHECK(destroyed == 10240003);
   memset(&chain, 0, sizeof(chain));
   chain.count = (size_t)-1;
   CHECK(!xe_spv_block_chain_append(&chain, nodes) && !nodes[0].next && !chain.head && !chain.tail);
   puts("PASS: 10240000 C89 block links, stable order, duplicate/overflow checks and next-before-free cleanup");
   return 0;
}

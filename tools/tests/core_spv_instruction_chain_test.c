#include "../../libretro/core_spv_instruction_chain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct node { unsigned value; void *next; } node;
static unsigned destroyed, sum;
static void **slot(void *p) { return &((node *)p)->next; }
static void visit(void *p) { ++destroyed; sum += ((node *)p)->value; }
static void release(void *p) { visit(p); free(p); }
int main(void)
{
   xe_spv_instruction_body body;
   node values[8], *p;
   unsigned round, i;
   static const unsigned order[8] = {3, 0, 1, 4, 2, 5, 6, 7};
   for (round = 0; round < 10000; ++round) {
      memset(&body, 0, sizeof(body));
      memset(values, 0, sizeof(values));
      for (i = 0; i < 8; ++i) values[i].value = i;
      CHECK(!xe_spv_instruction_insert(&body, NULL, NULL, values, slot));
      xe_spv_instruction_body_append(&body, values, slot);
      CHECK(body.chain.head == values && body.chain.tail == values && !body.penultimate);
      xe_spv_instruction_body_append(&body, values + 1, slot);
      xe_spv_instruction_body_append(&body, values + 2, slot);
      CHECK(body.penultimate == values + 1);
      CHECK(!xe_spv_instruction_insert(&body, values, values + 2, values + 3, slot));
      CHECK(!values[3].next && values[0].next == values + 1);
      CHECK(xe_spv_instruction_insert(&body, NULL, values, values + 3, slot));
      CHECK(xe_spv_instruction_insert(&body, values + 1, values + 2, values + 4, slot));
      CHECK(body.penultimate == values + 4);
      for (i = 5; i < 8; ++i) xe_spv_instruction_body_append(&body, values + i, slot);
      CHECK(body.penultimate == values + 6 && body.chain.tail == values + 7);
      for (i = 0, p = (node *)body.chain.head; p; ++i, p = (node *)p->next)
         CHECK(i < 8 && p->value == order[i]);
      CHECK(i == 8);
      destroyed = sum = 0;
      xe_spv_instruction_destroy(body.chain.head, slot, visit);
      CHECK(destroyed == 8 && sum == 28);
   }
   memset(&body, 0, sizeof(body));
   for (i = 0; i < 1024; ++i) {
      p = (node *)calloc(1, sizeof(*p));
      CHECK(p);
      p->value = i;
      xe_spv_instruction_body_append(&body, p, slot);
   }
   destroyed = sum = 0;
   xe_spv_instruction_destroy(body.chain.head, slot, release);
   CHECK(destroyed == 1024 && sum == 1023 * 512);
   xe_spv_instruction_destroy(NULL, slot, release);
   puts("PASS: C89 instruction append/splice, second-to-last, failed splice and free-safe teardown");
   return 0;
}

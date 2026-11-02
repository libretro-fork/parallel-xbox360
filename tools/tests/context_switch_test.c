/* Check the context-switch ABI with two suspends/resumes. MSVC C89. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
typedef void *fcontext_t;
typedef struct Transfer { fcontext_t context; void *data; } Transfer;
extern fcontext_t make_fcontext(void *stack, size_t size, void (*entry)(Transfer));
extern Transfer jump_fcontext(fcontext_t context, void *data);
static int phase;
static int first, second;
static void entry(Transfer transfer) {
  if (transfer.data != &first) phase = -1;
  else phase = 1;
  transfer = jump_fcontext(transfer.context, &first);
  if (transfer.data != &second || phase != 1) phase = -1;
  else phase = 2;
  (void)jump_fcontext(transfer.context, &second);
  abort();
}
int main(void) {
  unsigned char *stack = (unsigned char *)malloc(65536 + 16);
  void *top;
  fcontext_t context;
  Transfer transfer;
  int result;
  if (!stack) return 1;
  top = (void *)((uintptr_t)(stack + 65536 + 16) & ~(uintptr_t)15);
  context = make_fcontext(top, 65536, entry);
  transfer = jump_fcontext(context, &first);
  result = phase != 1 || transfer.data != &first;
  if (!result) {
    transfer = jump_fcontext(transfer.context, &second);
    result = phase != 2 || transfer.data != &second;
  }
  free(stack);
  if (!result) puts("Context switch: two resumes matched.");
  return result;
}

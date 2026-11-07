/* Actual deferred replay into a C COM mock; no GPU/device/CP is created. */
#define CINTERFACE
#define COBJMACROS
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4201)
#endif
#include <d3d12.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
extern void *xe_test_barriers_new(void);
extern void xe_test_barriers_delete(void *);
extern void *xe_test_barriers_append(void *);
extern void xe_test_barriers_pop(void *);
extern void xe_test_barriers_submit(void *);
extern void xe_test_barriers_marker(void *, unsigned);
extern void xe_test_barriers_copy(void *, const void *);
extern void xe_test_barriers_execute(void *, void *);
extern void xe_test_barriers_reset(void *);
extern void *xe_test_barriers_sized(size_t);
extern int xe_test_barriers_try_new(void);
extern int xe_test_barriers_try_submit(void *);
struct allocation_header { size_t size; uint64_t alignment; };
static size_t allocation_calls, live_bytes, peak_bytes;
static long fail_after = -1;
void *xe_test_command_alloc(size_t bytes) {
  struct allocation_header *header;
  if (fail_after == 0) return NULL;
  if (fail_after > 0) --fail_after;
  header = (struct allocation_header *)malloc(sizeof(*header) + bytes);
  if (!header) return NULL;
  ++allocation_calls; header->size = bytes; live_bytes += bytes;
  if (live_bytes > peak_bytes) peak_bytes = live_bytes;
  return header + 1;
}
void xe_test_command_free(void *p) {
  if (p) { struct allocation_header *header = (struct allocation_header *)p - 1;
    live_bytes -= header->size; free(header);
  }
}
#define ROWS 10000
struct expected { unsigned kind, id; const D3D12_RESOURCE_BARRIER *identity; };
static struct expected expected[ROWS * 2 + 32];
static D3D12_RESOURCE_BARRIER *saved[ROWS];
static unsigned expected_count, observed, calls, barrier_rows;
static ID3D12GraphicsCommandListVtbl vtable;
static ID3D12GraphicsCommandList mock;
static void push_expected(unsigned kind, unsigned id, const D3D12_RESOURCE_BARRIER *identity) {
  CHECK(expected_count < sizeof(expected)/sizeof(expected[0]));
  expected[expected_count].kind = kind; expected[expected_count].id = id;
  expected[expected_count++].identity = identity;
}
static void fill(D3D12_RESOURCE_BARRIER *b, unsigned id) {
  b->Type = (D3D12_RESOURCE_BARRIER_TYPE)(id % 3);
  b->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  if (b->Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
    b->Transition.pResource = (ID3D12Resource *)(uintptr_t)(id * 16 + 16);
    b->Transition.Subresource = id;
    b->Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b->Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  } else if (b->Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING) {
    b->Aliasing.pResourceBefore = (ID3D12Resource *)(uintptr_t)(id * 16 + 16);
    b->Aliasing.pResourceAfter = (ID3D12Resource *)(uintptr_t)(id * 16 + 32);
  } else b->UAV.pResource = (ID3D12Resource *)(uintptr_t)(id * 16 + 16);
}
static void verify(const D3D12_RESOURCE_BARRIER *b, unsigned id) {
  CHECK(b->Type == (D3D12_RESOURCE_BARRIER_TYPE)(id % 3));
  CHECK(b->Flags == D3D12_RESOURCE_BARRIER_FLAG_NONE);
  if (b->Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
    CHECK((uintptr_t)b->Transition.pResource == id * 16 + 16);
    CHECK(b->Transition.Subresource == id);
    CHECK(b->Transition.StateBefore == D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(b->Transition.StateAfter == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  } else if (b->Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING) {
    CHECK((uintptr_t)b->Aliasing.pResourceBefore == id * 16 + 16);
    CHECK((uintptr_t)b->Aliasing.pResourceAfter == id * 16 + 32);
  } else CHECK((uintptr_t)b->UAV.pResource == id * 16 + 16);
}
static void STDMETHODCALLTYPE resource_barrier(ID3D12GraphicsCommandList *self,
    UINT count, const D3D12_RESOURCE_BARRIER *rows) {
  UINT i; CHECK(self == &mock && count && rows); ++calls;
  for (i = 0; i < count; ++i) {
    const struct expected *e;
    CHECK(observed < expected_count); e = &expected[observed++];
    CHECK(e->kind == 1); verify(rows + i, e->id);
    if (e->identity) CHECK(rows + i == e->identity); /* No intermediate copy. */
    ++barrier_rows;
  }
}
static void STDMETHODCALLTYPE marker(ID3D12GraphicsCommandList *self, UINT value) {
  CHECK(self == &mock && observed < expected_count);
  CHECK(expected[observed].kind == 2 && expected[observed].id == value); ++observed;
}
static void run(void *commands, unsigned iteration) {
  unsigned i, batch_begin = 0;
  D3D12_RESOURCE_BARRIER external;
  expected_count = observed = barrier_rows = 0;
  xe_test_barriers_reset(commands);
  for (i = 0; i < ROWS; ++i) {
    D3D12_RESOURCE_BARRIER *row = (D3D12_RESOURCE_BARRIER *)xe_test_barriers_append(commands);
    CHECK(row); fill(row, i); saved[i] = row;
    if (i % 31 == 0) {
      xe_test_barriers_pop(commands);
      row = (D3D12_RESOURCE_BARRIER *)xe_test_barriers_append(commands); CHECK(row == saved[i]); fill(row, i);
    }
    if (i % 17 == 0) {
      xe_test_barriers_marker(commands, i); push_expected(2, i, NULL);
      xe_test_barriers_submit(commands);
      while (batch_begin <= i) { push_expected(1, batch_begin, saved[batch_begin]); ++batch_begin; }
    }
  }
  xe_test_barriers_marker(commands, iteration + ROWS); push_expected(2, iteration + ROWS, NULL);
  xe_test_barriers_submit(commands);
  while (batch_begin < ROWS) { push_expected(1, batch_begin, saved[batch_begin]); ++batch_begin; }
  fill(&external, ROWS); xe_test_barriers_copy(commands, &external); push_expected(1, ROWS, NULL);
  memset(&external, 0xcd, sizeof(external));
  for (i = 0; i < ROWS; ++i) verify(saved[i], i); /* All prior spans remain alive. */
  xe_test_barriers_execute(commands, &mock);
  CHECK(observed == expected_count && barrier_rows == ROWS + 1);
}
int main(void) {
  void *commands; unsigned i; size_t warmed_calls;
  memset(&vtable, 0, sizeof(vtable)); vtable.ResourceBarrier = resource_barrier; vtable.OMSetStencilRef = marker;
  mock.lpVtbl = &vtable;
  fail_after = 0; CHECK(!xe_test_barriers_try_new() && !live_bytes);
  fail_after = 1; CHECK(!xe_test_barriers_try_new() && !live_bytes);
  fail_after = -1;
  commands = xe_test_barriers_new(); CHECK(commands);
  run(commands, 0); warmed_calls = allocation_calls;
  for (i = 1; i < 20; ++i) run(commands, i);
  CHECK(allocation_calls == warmed_calls);
  xe_test_barriers_reset(commands);
  xe_test_barriers_append(commands); xe_test_barriers_pop(commands);
  expected_count = observed = 0;
  xe_test_barriers_submit(commands);
  xe_test_barriers_marker(commands, 99); push_expected(2, 99, NULL);
  xe_test_barriers_execute(commands, &mock); CHECK(observed == expected_count);
  /* Reset drops old replay packets while preserving not-yet-submitted rows. */
  xe_test_barriers_reset(commands); fill((D3D12_RESOURCE_BARRIER *)xe_test_barriers_append(commands), 77);
  xe_test_barriers_reset(commands); expected_count = observed = 0;
  xe_test_barriers_marker(commands, 88); push_expected(2, 88, NULL);
  xe_test_barriers_submit(commands); push_expected(1, 77, NULL);
  xe_test_barriers_execute(commands, &mock); CHECK(observed == expected_count);
  xe_test_barriers_delete(commands); CHECK(!live_bytes);
  /* First borrowed span commits; packet allocation for the second fails.
   * Retrying must record only the uncommitted tail, preserving row identities. */
  commands = xe_test_barriers_sized(24); CHECK(commands);
  expected_count = observed = 0;
  for (i = 0; i < 2050; ++i) {
    D3D12_RESOURCE_BARRIER *row = (D3D12_RESOURCE_BARRIER *)xe_test_barriers_append(commands);
    fill(row, i); push_expected(1, i, row);
  }
  fail_after = 0; CHECK(!xe_test_barriers_try_submit(commands));
  fail_after = -1; CHECK(xe_test_barriers_try_submit(commands));
  xe_test_barriers_execute(commands, &mock); CHECK(observed == expected_count);
  xe_test_barriers_delete(commands); CHECK(!live_bytes);
  printf("D3D12 mock replay: 200000 borrowed rows, exact pointer identity/order, stack-copy ownership and pending-reset preservation; %u calls\n", calls);
  printf("Recorder allocator: zero warmed allocations, zero live bytes at teardown; peak %lu bytes\n", (unsigned long)peak_bytes);
  return 0;
}

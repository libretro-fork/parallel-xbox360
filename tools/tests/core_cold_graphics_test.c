/* Actual cold graphics lifetime. Windows OS mapping oracle, MSVC C89. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
void *xe_test_graphics_new(void);
void *xe_test_graphics_registers(void *);
int xe_test_graphics_pause(void *);
void xe_test_graphics_free(void *);
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"cold graphics line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
int main(void) {
 unsigned i;
 void *graphics,*registers;
 MEMORY_BASIC_INFORMATION info;
 size_t released=0;
 for(i=0;i<128;++i) {
  graphics=xe_test_graphics_new();CHECK(graphics);
  registers=xe_test_graphics_registers(graphics);CHECK(registers);
  CHECK(VirtualQuery(registers,&info,sizeof(info)) && info.State==MEM_COMMIT);
  released+=info.RegionSize;
  CHECK(xe_test_graphics_pause(graphics));
  xe_test_graphics_free(graphics);
  CHECK(VirtualQuery(registers,&info,sizeof(info)) && info.State==MEM_FREE);
 }
 printf("PASS: 128 cold graphics owners released %lu register-mapping bytes, no device/JIT/setup\n",(unsigned long)released);
 return 0;
}

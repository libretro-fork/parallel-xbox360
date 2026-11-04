#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
static double seconds(void) { LARGE_INTEGER n, f; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f); return (double)n.QuadPart/(double)f.QuadPart; }
#else
#include <time.h>
static double seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (double)t.tv_sec+(double)t.tv_nsec*1e-9; }
#endif
void *input_fixture_new(void);
void input_fixture_free(void*);
void input_fixture_poll(void*);
uint32_t input_fixture_read(void*);
int input_fixture_validate(void);
static volatile uint64_t checksum;
int main(void) {
   void *input = input_fixture_new();
   unsigned i, batch;
   double start;
   uint64_t sum;
   if (!input) return 1;
   if (!input_fixture_validate()) return 4;
   for (i = 0; i < 1000; ++i) input_fixture_poll(input);
   if (input_fixture_read(input) != 1000) return 2;
   if (getenv("INPUT_DRIVER_VERIFY_ONLY")) {
      input_fixture_free(input);
      puts("PASS: actual input adapter mapping, hotplug, keystrokes and rumble");
      return 0;
   }
   for (batch = 0; batch < 5; ++batch) {
      sum = 0; start = seconds();
      for (i = 0; i < 1000000; ++i) sum += input_fixture_read(input);
      printf("read_ns %.3f\n",(seconds()-start)*1e3); checksum = sum;
      start = seconds();
      for (i = 0; i < 50000; ++i) input_fixture_poll(input);
      printf("poll_ns %.3f\n",(seconds()-start)*2e4);
   }
   input_fixture_free(input);
   return checksum ? 0 : 3;
}

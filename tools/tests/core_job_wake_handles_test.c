/* Compiler-worker resource lifetimes. MSVC C89, real pool implementation. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
#include "libretro/core_job_pool.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"job handles line %u\n",(unsigned)__LINE__);return 0;}}while(0)
static void unused_run(void* a,void* b,void* c,uint64_t d){(void)a;(void)b;(void)c;(void)d;abort();}
static void unused_discard(void* a,void* b){(void)a;(void)b;abort();}
static double now(void){
#if defined(_WIN32)
 return (double)GetTickCount64()/1000.0;
#else
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec+(double)t.tv_nsec/1e9;
#endif
}
static int cycle(void){xe_job_pool* pool;size_t i;int ready;double limit;
 pool=xe_job_pool_new(16,sizeof(unsigned),"Worker lifetime",unused_run,unused_discard,NULL,NULL,NULL,NULL);CHECK(pool);
 CHECK(xe_job_pool_set_threads(pool,4));limit=now()+5;
 do{ready=1;
#if defined(_WIN32)
 for(i=0;i<4;++i)if(!((uintptr_t)retro_atomic_load_acquire_ptr(&pool->workers[i]->work.waitlist)&~(uintptr_t)1)){ready=0;break;}
#else
 (void)i;ready=retro_atomic_load_acquire_int(&pool->parked_count)==4;
#endif
 CHECK(now()<limit);}while(!ready);
 xe_job_pool_free(pool);return 1;}
int main(void){unsigned i;
#if defined(_WIN32)
 typedef BOOL (WINAPI *get_count_t)(HANDLE,PDWORD);get_count_t get_count;DWORD before,after;
 retro_eventcount_t prime;int key;
 if(_putenv("RETRO_EVENTCOUNT_WIN32=event") || !retro_eventcount_init(&prime))return 2;
 key=retro_eventcount_prepare_wait(&prime);retro_eventcount_commit_wait_timeout(&prime,key,1000);
 retro_eventcount_free(&prime);
#endif
 if(!cycle())return 3;
#if defined(_WIN32)
 if(strcmp(retro_eventcount_backend_name(),"win32 event"))return 4;
 get_count=(get_count_t)(void(*)(void))GetProcAddress(GetModuleHandleA("kernel32.dll"),"GetProcessHandleCount");
 if(!get_count || !get_count(GetCurrentProcess(),&before))return 5;
#endif
 for(i=0;i<32;++i)if(!cycle())return 6;
#if defined(_WIN32)
 if(!get_count(GetCurrentProcess(),&after) || after!=before){fprintf(stderr,"worker handle growth\n");return 7;}
 puts("PASS: 32 four-worker lifetimes, forced Windows event backend, no handle growth");
#else
 puts("PASS: 32 four-worker lifetimes and joined release; Windows handle accounting is checked on Windows");
#endif
 retro_eventcount_thread_cleanup();return 0;}

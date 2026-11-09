/* Test-profile DSO API and normal-release absence, without starting a game. */
#include "libretro/core_draw_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"draw profile line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
typedef unsigned (*Enter)(unsigned);
typedef void (*Leave)(unsigned);
typedef unsigned (*Role)(void);
typedef void (*Event)(unsigned,unsigned);
typedef void (*Reset)(void);
typedef int (*Snapshot)(xe_draw_stats*,size_t);
typedef size_t (*Violations)(const xe_draw_stats*);
int main(int argc,char**argv){
#ifdef _WIN32
 HMODULE module;FARPROC symbol;
#else
 void* module;void* symbol;
#endif
 Enter enter;Leave leave;Role role;Event event;Reset reset;Snapshot snapshot;Violations violations;
 struct guarded {uint64_t first;xe_draw_stats stats;uint64_t last;} value,copy;
 unsigned previous;int expected;
 const char* names[]={"xe_draw_stats_enter","xe_draw_stats_leave","xe_draw_stats_role","xe_draw_stats_event","xe_draw_stats_reset","xe_draw_stats_snapshot","xe_draw_stats_violations"};
 unsigned i;
 CHECK(argc==3);expected=!strcmp(argv[2],"stats");CHECK(expected || !strcmp(argv[2],"release"));
#ifdef _WIN32
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);module=LoadLibraryA(argv[1]);
#define LOOKUP(name) GetProcAddress(module,name)
#else
 module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
#define LOOKUP(name) dlsym(module,name)
#endif
 CHECK(module);
 if(!expected){for(i=0;i<sizeof(names)/sizeof(*names);++i)CHECK(!LOOKUP(names[i]));}
 else{
#define FIND(fn,n) do{symbol=LOOKUP(n);CHECK(symbol && sizeof(fn)==sizeof(symbol));memcpy(&fn,&symbol,sizeof(fn));}while(0)
  FIND(enter,names[0]);FIND(leave,names[1]);FIND(role,names[2]);FIND(event,names[3]);FIND(reset,names[4]);FIND(snapshot,names[5]);FIND(violations,names[6]);
  memset(&value,0xa5,sizeof(value));memcpy(&copy,&value,sizeof(copy));
  CHECK(!snapshot(&value.stats,sizeof(value.stats)-1));CHECK(!memcmp(&copy,&value,sizeof(copy)));
  CHECK(!snapshot(&value.stats,sizeof(value.stats)+1));CHECK(!memcmp(&copy,&value,sizeof(copy)));
  reset();CHECK(snapshot(&value.stats,sizeof(value.stats)));CHECK(!violations(&value.stats));CHECK(value.first==copy.first && value.last==copy.last);
  previous=enter(XE_DRAW_GPU);event(XE_DRAW_UTILITY_PIPELINE,XE_DRAW_GRAPHICS_PSO);CHECK(role()==XE_DRAW_GPU);leave(previous);
  CHECK(snapshot(&value.stats,sizeof(value.stats)));CHECK(violations(&value.stats)==1);
  previous=enter(XE_DRAW_COMPILER);event(XE_DRAW_GUEST_PIPELINE,XE_DRAW_MODULE);leave(previous);
  CHECK(snapshot(&value.stats,sizeof(value.stats)));CHECK(violations(&value.stats)==1 && value.stats.events[XE_DRAW_COMPILER][XE_DRAW_GUEST_PIPELINE][XE_DRAW_MODULE]==1);
  reset();CHECK(snapshot(&value.stats,sizeof(value.stats)));CHECK(!violations(&value.stats));
 }
#ifdef _WIN32
 CHECK(FreeLibrary(module));
#else
 CHECK(!dlclose(module));
#endif
 puts(expected?"PASS: stats-profile DSO API, bounded snapshot canaries and rejected injected critical events (not gameplay signoff)":"PASS: normal release DSO exports no draw-stats API");return 0;
}

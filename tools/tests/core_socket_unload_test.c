/* Run under a parent timeout: DLL detach must not join a live socket worker. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void (*Start)(void (*retired)(void));
typedef void (*Stop)(void);
static unsigned completed;
static void retired(void) { ++completed; }
int main(int argc,char **argv)
{
   unsigned i;
   int legacy=argc==3 && !strcmp(argv[2],"legacy");
   HMODULE module;
   Start start;
   Stop stop;
   SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
   if(argc!=2 && !legacy) return 2;
   for(i=0;i<64;++i) {
      module=LoadLibraryA(argv[1]); if(!module) return 3;
      start=(Start)(void*)GetProcAddress(module,"ProbeStart");
      stop=(Stop)(void*)GetProcAddress(module,"ProbeStop");
      if(!start || !stop) return 4;
      start(retired);
      stop();
      if(!legacy && completed!=i+1) return 5;
      if(!FreeLibrary(module)) return 6;
      if(GetModuleHandleA(argv[1])) return 7;
   }
   puts("Socket I/O: 64 pending-operation retirements and DLL unloads: PASS");
   return 0;
}
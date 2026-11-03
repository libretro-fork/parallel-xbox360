/* Cache-hot XML parse/query components; host profiling only. */
#include "libretro/core_xlast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
static double now(void) {
  LARGE_INTEGER t,f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
  return (double)t.QuadPart/(double)f.QuadPart;
}
static unsigned long rss(void) {
  PROCESS_MEMORY_COUNTERS p;
  p.cb=sizeof(p);
  return GetProcessMemoryInfo(GetCurrentProcess(),&p,sizeof(p))?
    (unsigned long)(p.PeakWorkingSetSize/1024):0;
}
#else
#include <time.h>
#include <sys/resource.h>
static double now(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return (double)t.tv_sec+(double)t.tv_nsec*0.000000001;
}
static unsigned long rss(void) {
  struct rusage p; return getrusage(RUSAGE_SELF,&p)?0:(unsigned long)p.ru_maxrss;
}
#endif
void *xe_pugi_open(const void *,size_t);
void xe_pugi_close(void *);
const char *xe_pugi_text(void *,const char *);
struct handle { void *doc; char *old_input; };
static struct handle open_doc(unsigned mode,const char *xml,size_t length) {
  struct handle p;
  char *input=(char *)malloc(length+1);
  if(!input) exit(3);
  memcpy(input,xml,length+1); p.old_input=NULL;
  if(mode&1) p.doc=xe_xlast_parse_owned(input,length);
  else { p.old_input=input; p.doc=xe_pugi_open(input,length); }
  if(!p.doc) exit(4);
  return p;
}
static void close_doc(unsigned mode,struct handle p) {
  if(mode&1) xe_xlast_destroy((xe_xlast *)p.doc);
  else { xe_pugi_close(p.doc); free(p.old_input); }
}
static const char *lookup(unsigned mode,struct handle p,unsigned id,const char *path) {
  return mode&1?xe_xlast_localized((xe_xlast *)p.doc,id,1):xe_pugi_text(p.doc,path);
}
int main(int argc,char **argv) {
  static const char begin[]="<XboxLiveSubmissionProject><GameConfigProject titleName='Title'><LocalizedStrings>";
  static const char end[]="</LocalizedStrings></GameConfigProject></XboxLiveSubmissionProject>";
  char *xml,path[256];
  size_t length;
  unsigned mode,n,i,id;
  unsigned long iterations,at;
  struct handle p;
  volatile uint32_t checksum=0;
  double start,seconds;
  if(argc!=4) return 2;
  mode=(unsigned)strtoul(argv[1],NULL,10); n=(unsigned)strtoul(argv[2],NULL,10);
  iterations=strtoul(argv[3],NULL,10);
  if(mode>5||!n||n>10000||!iterations) return 2;
  xml=(char *)malloc((size_t)n*160+256); if(!xml) return 3;
  strcpy(xml,begin); length=strlen(begin);
  for(i=0;i<n;++i) length+=(size_t)sprintf(xml+length,
    "<LocalizedString id='%u'><Text locale='en-US'>%s</Text></LocalizedString>",
    i,i%10?"Value text":"Value &amp; text");
  strcpy(xml+length,end); length+=strlen(end); id=n/2;
  sprintf(path,"/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings/LocalizedString[@id='%u']/Text[@locale='en-US']",id);
  p=open_doc(mode,xml,length);
  if(strcmp(lookup(mode,p,id,path),id%10?"Value text":"Value & text")) return 5;
  if(mode<2) close_doc(mode,p);
  start=now();
  for(at=0;at<iterations;++at) {
    if(mode<2) { p=open_doc(mode,xml,length); checksum+=(p.doc!=NULL); close_doc(mode,p); }
    else {
      if(mode>=4) {
        id=((uint32_t)at*15485863U)%n;
        if(!(mode&1)) sprintf(path,"/XboxLiveSubmissionProject/GameConfigProject/LocalizedStrings/LocalizedString[@id='%u']/Text[@locale='en-US']",id);
      }
      checksum+=(uint32_t)strlen(lookup(mode,p,id,path));
    }
  }
  seconds=now()-start;
  if(mode>=2) close_doc(mode,p);
  printf("%u,%u,%lu,%lu,%.9f,%.3f,%lu,%u\n",mode,n,(unsigned long)length,iterations,
    seconds,seconds*1000000000.0/(double)iterations,rss(),(unsigned)checksum);
  free(xml); return seconds>0?0:6;
}

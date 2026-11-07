/* Hostile VFS signature probes. New algorithms/tests are MSVC C89. */
#include "libretro/core_file_probe.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef XE_PROBE_NATIVE
int xe_file_signature_native(const char *);
#endif
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"probe line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static unsigned char head[4],tail[4];
static int64_t length,position;
static unsigned opens,closes,reads,seeks,transferred,limit;
static int fault;
static struct retro_vfs_file_handle *RETRO_CALLCONV open_file(const char *path,unsigned mode,unsigned hints) {
 CHECK(mode==RETRO_VFS_FILE_ACCESS_READ && hints==RETRO_VFS_FILE_ACCESS_HINT_NONE);
 ++opens;if(strcmp(path,"content://probe") || fault==1) return NULL;
 position=0;return (struct retro_vfs_file_handle *)head;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle *p) {(void)p;++closes;return fault==8?-1:0;}
static int64_t RETRO_CALLCONV size_file(struct retro_vfs_file_handle *p) {(void)p;return fault==2?-1:length;}
static int64_t RETRO_CALLCONV tell_file(struct retro_vfs_file_handle *p) {(void)p;return position;}
static int64_t RETRO_CALLCONV seek_file(struct retro_vfs_file_handle *p,int64_t at,int origin) {
 (void)p;++seeks;if(fault==5 || origin!=RETRO_VFS_SEEK_POSITION_START || at<0 || at>length) return -1;
 position=at;return position;
}
static int64_t RETRO_CALLCONV read_file(struct retro_vfs_file_handle *p,void *out,uint64_t size) {
 unsigned char *bytes=(unsigned char *)out;unsigned count=0;
 (void)p;++reads;CHECK(size && size<=4);
 if(fault==3) return -1;
 if(fault==4) return 0;
 if(fault==6) {fault=0;errno=EINTR;return -1;}
 if(fault==7) return (int64_t)size+1;
 while(count<size && count<limit && position<length) {
  if(fault==9 && position>=2) break;
  if(fault==10 && position>=length-2) break;
  if(position<4) bytes[count]=head[(unsigned)position];
  else {CHECK(position>=length-4);bytes[count]=tail[(unsigned)(position-(length-4))];}
  ++position;++count;
 }
 transferred+=count;return count;
}
static void setup(const void *h,int64_t size) {
 memcpy(head,h,4);memcpy(tail,"TAIL",4);length=size;position=0;
 opens=closes=reads=seeks=transferred=0;limit=4;fault=0;
}
int main(void) {
 const char magic[14][5]={"XEX0","XEX?","XEX-","XEX%","XEX1","XEX2",{127,'E','L','F'},"CON ","LIVE","PIRS",{'X','S','F',26},"XBEH",{'M','Z','x','x'},"junk"};
 const int expected[14]={0,1,2,3,4,5,6,7,8,9,10,11,13,12};
 const unsigned char zar[4]={0x16,0x9f,0x52,0xd6};
 struct retro_vfs_interface iface;
 struct retro_vfs_interface_info info;
 xe_file_probe probe,empty;
 unsigned char out[4];unsigned i,n;
 memset(&iface,0,sizeof(iface));iface.open=open_file;iface.close=close_file;
 iface.size=size_file;iface.tell=tell_file;iface.seek=seek_file;iface.read=read_file;
 info.required_interface_version=2;info.iface=&iface;filestream_vfs_init(&info);
 memset(&probe,0,sizeof(probe));memset(&empty,0,sizeof(empty));
 for(i=0;i<14;++i) {
  setup(magic[i],100);CHECK(xe_file_probe_open(&probe,"content://probe"));
  CHECK(opens==1 && reads==1 && transferred==4 && !seeks && !closes);
  CHECK(!memcmp(probe.head,magic[i],4));CHECK(!xe_file_probe_open(&probe,"content://probe"));
  CHECK(xe_file_probe_close(&probe) && closes==1 && xe_file_probe_close(&probe) && closes==1);
#ifdef XE_PROBE_NATIVE
  setup(magic[i],100);if(i==13) memcpy(tail,zar,4);
  CHECK(xe_file_signature_native("content://probe")==expected[i]);
  CHECK(opens==1 && closes==1 && transferred==(i==13?8u:4u) && seeks==(i==13?1u:0u));
#else
  (void)expected;
#endif
 }
 for(n=1;n<=4;++n) {
  setup("ABCD",INT64_MAX);limit=n;memcpy(tail,zar,4);
  CHECK(xe_file_probe_open(&probe,"content://probe"));
  CHECK(xe_file_probe_tail(&probe,out) && !memcmp(out,zar,4));
  CHECK(opens==1 && transferred==8 && seeks==1 && position==INT64_MAX);
  CHECK(xe_file_probe_close(&probe));
 }
 setup(zar,4);CHECK(xe_file_probe_open(&probe,"content://probe"));
 CHECK(xe_file_probe_tail(&probe,out) && !memcmp(out,zar,4) && reads==1 && !seeks);
 CHECK(xe_file_probe_close(&probe));
 for(i=0;i<4;++i) {
  setup("XEX2",i);CHECK(!xe_file_probe_open(&probe,"content://probe"));
  CHECK(opens==1 && closes==1 && !reads && !memcmp(&probe,&empty,sizeof(probe)));
 }
 for(i=1;i<=9;++i) if(i!=5 && i!=6 && i!=8) {
  setup("XEX2",100);fault=(int)i;
  CHECK(!xe_file_probe_open(&probe,"content://probe"));
  CHECK(opens==1 && closes==(i==1?0u:1u) && !memcmp(&probe,&empty,sizeof(probe)));
#ifdef XE_PROBE_NATIVE
  setup("XEX2",100);fault=(int)i;
  CHECK(xe_file_signature_native("content://probe")==14 && opens==1 && closes==(i==1?0u:1u));
#endif
 }
 setup("XEX2",100);fault=6;CHECK(xe_file_probe_open(&probe,"content://probe"));
 CHECK(reads==2 && transferred==4);CHECK(xe_file_probe_close(&probe));
 for(i=3;i<=10;++i) if(i!=6 && i!=8 && i!=9) {
  setup("junk",100);CHECK(xe_file_probe_open(&probe,"content://probe"));fault=(int)i;
  memset(out,0xa5,4);CHECK(!xe_file_probe_tail(&probe,out));
  CHECK(out[0]==0xa5 && out[1]==0xa5 && out[2]==0xa5 && out[3]==0xa5);
  CHECK(xe_file_probe_close(&probe) && closes==1);
 }
 setup("XEX2",100);CHECK(xe_file_probe_open(&probe,"content://probe"));fault=8;
 CHECK(!xe_file_probe_close(&probe) && closes==1 && !memcmp(&probe,&empty,sizeof(probe)));
 CHECK(xe_file_probe_close(&probe) && closes==1);
 CHECK(!xe_file_probe_open(&probe,NULL) && !xe_file_probe_tail(&probe,out));
 puts("PASS: single-open VFS probes, all launch signatures, sparse 64-bit tails, faults and nonpublication");
 return 0;
}

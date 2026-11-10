/* C89 VFS-only URI, borrowed sectors, cursor concurrency and real SVOD mount. */
#include "libretro/core_svod.h"
#include <streams/file_stream.h>
#include <retro_dirent.h>
#include <file/file_path.h>
#include <rthreads/rthreads.h>
#include <retro_atomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define CHECK(x) do {if(!(x)){fprintf(stderr,"svod line %d\n",__LINE__);exit(1);}} while(0)
#define FRAGMENT_SIZE 32768
static unsigned char contents[4][40000];
static size_t lengths[4]={FRAGMENT_SIZE,FRAGMENT_SIZE,FRAGMENT_SIZE,0};
static struct handle {unsigned file,active;int64_t at;} handles[8];
static struct directory {unsigned at,active;} directory;
static unsigned fault,limit=7,mapping,opens,closes,dirs,closedirs,enumerations,position_seeks;
static retro_atomic_int_t reads,bytes,start,bad;
static unsigned char* direct;static size_t direct_size;
static const char* entries[]={"Data0002","Data0000","Data0001"};
static const char* name_at(unsigned index) {
   if(fault==11 && index==0) return "data0000";
   if(fault==13 && enumerations==2 && index==0) return "Data0002-longer";
   return entries[index];
}
static struct retro_vfs_file_handle* RETRO_CALLCONV open_file(const char* path,unsigned mode,unsigned hint) {
   unsigned id,i;
   (void)hint;CHECK(mode==RETRO_VFS_FILE_ACCESS_READ);++opens;
   if(!strcmp(path,"content://svod/game")) id=3;
   else if(!strcmp(path,"content://svod/game.data/Data0000")) id=0;
   else if(!strcmp(path,"content://svod/game.data/Data0001")) id=1;
   else if(!strcmp(path,"content://svod/game.data/Data0002")) id=2;
   else return NULL;
   if(fault==4 && id==1) return NULL;
   for(i=0;i<8;++i) if(!handles[i].active) {
      handles[i].active=1;handles[i].file=id;handles[i].at=0;return (struct retro_vfs_file_handle*)(handles+i);
   }
   return NULL;
}
static int RETRO_CALLCONV close_file(struct retro_vfs_file_handle* file) {
   struct handle* h=(struct handle*)file;CHECK(h->active);h->active=0;++closes;return 0;
}
static int64_t RETRO_CALLCONV size_file(struct retro_vfs_file_handle* file) {
   struct handle* h=(struct handle*)file;
   if(fault==5 && h->file==1) return -1;
   if(fault==12) return INT64_MAX;
   return (int64_t)lengths[h->file];
}
static int64_t RETRO_CALLCONV tell_file(struct retro_vfs_file_handle* file) {return ((struct handle*)file)->at;}
static int64_t RETRO_CALLCONV seek_file(struct retro_vfs_file_handle* file,int64_t at,int origin) {
   struct handle* h=(struct handle*)file;
   if(fault==6 || at<0 || origin!=RETRO_VFS_SEEK_POSITION_START) return -1;
   if(fault==10) return at+1;
   h->at=at;return position_seeks?at:0;
}
static int64_t RETRO_CALLCONV read_file(struct retro_vfs_file_handle* file,void* out,uint64_t count) {
   struct handle* h=(struct handle*)file;size_t n;
   retro_atomic_fetch_add_int(&reads,1);
   if(fault==7) {fault=0;errno=EINTR;return -1;}
   if(fault==9) return (int64_t)count+1;
   if(fault==8 && retro_atomic_load_relaxed_int(&bytes)>=5) return 0;
   if(h->at<0 || (uint64_t)h->at>=lengths[h->file]) return 0;
   n=lengths[h->file]-(size_t)h->at;if(n>count) n=(size_t)count;if(n>limit) n=limit;
   if(fault==8 && n>5-(size_t)retro_atomic_load_relaxed_int(&bytes)) n=5-(size_t)retro_atomic_load_relaxed_int(&bytes);
   if(direct) CHECK((uintptr_t)out>=(uintptr_t)direct && n<=direct_size &&
         (uintptr_t)out-(uintptr_t)direct<=direct_size-n);
   memcpy(out,contents[h->file]+(size_t)h->at,n);h->at+=(int64_t)n;
   retro_atomic_fetch_add_int(&bytes,(int)n);return (int64_t)n;
}
static const uint8_t* mapped(void* file,int64_t* size) {
   struct handle* h=(struct handle*)file;
   if(!mapping) {*size=0;return NULL;}*size=(int64_t)lengths[h->file];return contents[h->file];
}
static struct retro_vfs_dir_handle* RETRO_CALLCONV open_dir(const char* path,bool hidden) {
   CHECK(hidden);++dirs;
   if(fault==1 || strcmp(path,"content://svod/game.data")) return NULL;
   CHECK(!directory.active);directory.active=1;directory.at=0;++enumerations;return (struct retro_vfs_dir_handle*)&directory;
}
static bool RETRO_CALLCONV read_dir(struct retro_vfs_dir_handle* opaque) {
   struct directory* d=(struct directory*)opaque;
   if(d->at>=(fault==2?2u:3u)) return false;
   ++d->at;return true;
}
static const char* RETRO_CALLCONV get_name(struct retro_vfs_dir_handle* opaque) {return name_at(((struct directory*)opaque)->at-1);}
static bool RETRO_CALLCONV is_dir(struct retro_vfs_dir_handle* opaque) {(void)opaque;return fault==3;}
static int RETRO_CALLCONV close_dir(struct retro_vfs_dir_handle* opaque) {
   struct directory* d=(struct directory*)opaque;CHECK(d->active);d->active=0;++closedirs;return 0;
}
static void configure(unsigned map) {
   struct retro_vfs_interface api;struct retro_vfs_interface_info info;
   memset(&api,0,sizeof(api));api.open=open_file;api.close=close_file;api.size=size_file;
   api.tell=tell_file;api.seek=seek_file;api.read=read_file;api.opendir=open_dir;
   api.readdir=read_dir;api.dirent_get_name=get_name;api.dirent_is_dir=is_dir;api.closedir=close_dir;
   info.iface=&api;info.required_interface_version=3;
   filestream_vfs_init(&info);dirent_vfs_init(&info);filestream_set_mapped_ptr_cb(mapped);
   mapping=map;fault=0;opens=closes=dirs=closedirs=enumerations=0;
   retro_atomic_int_init(&reads,0);retro_atomic_int_init(&bytes,0);
}
static void closed(void) {unsigned i;CHECK(!directory.active);for(i=0;i<8;++i) CHECK(!handles[i].active);}
static xe_svod_files* shared;
static void reader(void* arg) {
   unsigned i,j,id=(unsigned)(uintptr_t)arg;unsigned char out[64];size_t off;
   while(!retro_atomic_load_acquire_int(&start)) {}
   for(i=0;i<1000;++i) {
      off=(i*17+id*101)%(FRAGMENT_SIZE-64);
      if(!xe_svod_files_read(shared,i%3,off,out,64)) {retro_atomic_store_release_int(&bad,1);return;}
      for(j=0;j<64;++j) if(out[j]!=contents[i%3][off+j]) retro_atomic_store_release_int(&bad,1);
   }
}
static void codec(void) {
   unsigned i;uint8_t input;char out[4];size_t n;
   for(i=0;i<256;++i) {
      input=(uint8_t)i;n=xe_svod_name_size(&input,1);memset(out,0x55,sizeof(out));
      CHECK(n>=1 && n<=3 && xe_svod_name_write(out,n,&input,1) && out[n]==0x55);
      CHECK(!xe_svod_name_write(out,n-1,&input,1));
#ifdef _WIN32
      {WCHAR wide;char wanted[4];int count;
       CHECK(MultiByteToWideChar(1252,0,(const char*)&input,1,&wide,1)==1);
       count=WideCharToMultiByte(CP_UTF8,0,&wide,1,wanted,4,NULL,NULL);
       CHECK(count==(int)n && !memcmp(out,wanted,n));}
#endif
   }
   input=0x80;CHECK(xe_svod_name_size(&input,1)==3 && xe_svod_name_write(out,3,&input,1));
   CHECK(!memcmp(out,"\xE2\x82\xAC",3));
}
#ifdef XE_SVOD_NATIVE
extern size_t xe_svod_native_header(void*,size_t);
extern void* xe_svod_native_open(const char*);
extern void xe_svod_native_close(void*);
extern int xe_svod_native_read(void*,const char*,size_t,void*,size_t,size_t*);
static void put16(unsigned char* p,unsigned v) {p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char* p,unsigned v) {put16(p,v);put16(p+2,v>>16);}
static void native(void) {
   void* device;unsigned i,map;unsigned char out[32];size_t got;
   limit=40000;
   memset(contents,0,sizeof(contents));lengths[3]=xe_svod_native_header(contents[3],40000);CHECK(lengths[3]);
   memcpy(contents[0]+0x2000,"MICROSOFT*XBOX*MEDIA",20);
   put32(contents[0]+0x2014,0);put32(contents[0]+0x2018,64);
   put16(contents[0]+0x3002,8);put32(contents[0]+0x3004,4);put32(contents[0]+0x3008,17);
   contents[0][0x300C]=0x80;contents[0][0x300D]=5;memcpy(contents[0]+0x300E,"B.bin",5);
   put32(contents[0]+0x3024,5);put32(contents[0]+0x3028,21);
   contents[0][0x302C]=0x80;contents[0][0x302D]=8;memcpy(contents[0]+0x302E,"caf\xE9.bin",8);
   for(i=0;i<32;++i) {contents[0][0x5000+i]=(unsigned char)(i+1);contents[0][0x5800+i]=(unsigned char)(99+i);}
   for(map=0;map<2;++map) {
      configure(map);device=xe_svod_native_open("content://svod/game");CHECK(device);
      printf("SVOD mount map=%u: %d VFS read calls\n",map,retro_atomic_load_relaxed_int(&reads));
      memset(out,0,sizeof(out));CHECK(xe_svod_native_read(device,"B.bin",0,out,32,&got) && got==17);
      CHECK(!memcmp(out,contents[0]+0x5000,17));
      CHECK(xe_svod_native_read(device,"caf\xC3\xA9.bin",0,out,32,&got) && got==21);
      CHECK(!memcmp(out,contents[0]+0x5800,21));
      if (!map) {
         fault=8;retro_atomic_store_release_int(&bytes,0);
         CHECK(!xe_svod_native_read(device,"B.bin",0,out,32,&got) && got==5);
         fault=0;CHECK(xe_svod_native_read(device,"B.bin",0,out,32,&got) && got==17);
      }
      xe_svod_native_close(device);closed();
   }
   /* The final four bytes of a directory sector may contain only padding. */
   put32(contents[0]+0x2018,2048);put16(contents[0]+0x3022,511);put16(contents[0]+0x37FC,0xFFFF);
   configure(0);device=xe_svod_native_open("content://svod/game");CHECK(device);
   xe_svod_native_close(device);closed();
   put32(contents[0]+0x2018,64);put16(contents[0]+0x3022,0);
   /* Left recursion replaces the root's fallback slot before its right sibling. */
   memcpy(contents[0]+0x7000,contents[0]+0x5000,32);
   memset(contents[0]+0x5000,0,2048);put32(contents[0]+0x5004,9);put32(contents[0]+0x5008,17);
   contents[0][0x500C]=0x80;contents[0][0x500D]=5;memcpy(contents[0]+0x500E,"A.bin",5);
   memset(contents[0]+0x7800,42,17);
   put16(contents[0]+0x3000,2048);put32(contents[0]+0x3004,8);put32(contents[0]+0x2018,0x2100);
   for(map=0;map<2;++map) {
      configure(map);device=xe_svod_native_open("content://svod/game");CHECK(device);
      CHECK(xe_svod_native_read(device,"B.bin",0,out,32,&got) && got==17 && !memcmp(out,contents[0]+0x7000,17));
      CHECK(xe_svod_native_read(device,"A.bin",0,out,32,&got) && got==17 && out[0]==42);
      CHECK(xe_svod_native_read(device,"caf\xC3\xA9.bin",0,out,32,&got) && got==21);
      xe_svod_native_close(device);closed();
   }
   memcpy(contents[0]+0x5000,contents[0]+0x7000,32);
   put16(contents[0]+0x3000,0);put32(contents[0]+0x3004,4);put32(contents[0]+0x2018,64);
   /* Untrusted layout/tree fields fail without native exceptions or OOB reads. */
   contents[0][0x2000]=0;configure(0);CHECK(!xe_svod_native_open("content://svod/game"));closed();contents[0][0x2000]='M';
   put32(contents[0]+0x2018,8);configure(0);CHECK(!xe_svod_native_open("content://svod/game"));closed();put32(contents[0]+0x2018,64);
   contents[0][0x300D]=255;configure(0);CHECK(!xe_svod_native_open("content://svod/game"));closed();contents[0][0x300D]=5;
   put16(contents[0]+0x3022,8);configure(0);CHECK(!xe_svod_native_open("content://svod/game"));closed();put16(contents[0]+0x3022,0);
   put32(contents[0]+0x2014,UINT32_MAX);configure(0);CHECK(!xe_svod_native_open("content://svod/game"));closed();put32(contents[0]+0x2014,0);
   puts("PASS: real SVOD mount, cached directory names and direct payload transfer through VFS-only URI");
}
#endif
static void native_filesystem(const char* root) {
   struct retro_vfs_interface_info info;char path[4096],directory_path[4096];
   RFILE* file;xe_svod_files* files;const uint8_t* view;unsigned i;size_t root_bytes;
   info.iface=NULL;info.required_interface_version=3;
   filestream_set_mapped_ptr_cb(NULL);filestream_vfs_init(&info);dirent_vfs_init(&info);path_vfs_init(&info);
   root_bytes=strlen(root);
   CHECK(root_bytes<=sizeof(directory_path)-sizeof(".data"));
   memcpy(directory_path,root,root_bytes);
   memcpy(directory_path+root_bytes,".data",sizeof(".data"));
   CHECK(path_mkdir(directory_path));
   root_bytes+=sizeof(".data")-1;
   CHECK(root_bytes<=sizeof(path)-sizeof("/Data0000"));
   for(i=0;i<3;++i) {
      memcpy(path,directory_path,root_bytes);
      memcpy(path+root_bytes,"/Data0000",sizeof("/Data0000"));
      path[root_bytes+8]=(char)('0'+i);
      file=filestream_open(path,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);CHECK(file);
      CHECK(filestream_write(file,contents[i],(int64_t)lengths[i])==(int64_t)lengths[i]);CHECK(!filestream_close(file));
   }
   files=xe_svod_files_open(root,3);CHECK(files);
   view=xe_svod_files_view(files,0,2048,2048);CHECK(view && !memcmp(view,contents[0]+2048,2048));
   xe_svod_files_close(files);
#ifdef XE_SVOD_NATIVE
   file=filestream_open(root,RETRO_VFS_FILE_ACCESS_WRITE,RETRO_VFS_FILE_ACCESS_HINT_NONE);CHECK(file);
   CHECK(filestream_write(file,contents[3],(int64_t)lengths[3])==(int64_t)lengths[3]);CHECK(!filestream_close(file));
   {void* device=xe_svod_native_open(root);unsigned char out[32];size_t got;
    CHECK(device && xe_svod_native_read(device,"B.bin",0,out,32,&got) && got==17);
    CHECK(!memcmp(out,contents[0]+0x5000,17));xe_svod_native_close(device);}
#endif
   puts("PASS: native directory fallback and immutable OS-mapped fragment views");
}
int main(int argc,char** argv) {
   unsigned map,i,j;size_t got;unsigned before;unsigned char out[64];const uint8_t* view;
   xe_svod_files* files;xe_svod_sectors cache;sthread_t* threads[4];
   codec();
   for(i=0;i<3;++i) for(j=0;j<FRAGMENT_SIZE;++j) contents[i][j]=(unsigned char)(i*31+j);
   for(map=0;map<4;++map) {
      configure(map&1);position_seeks=map>>1;files=xe_svod_files_open("content://svod/game",3);CHECK(files && opens==3 && closedirs==2);
      CHECK(xe_svod_files_size(files)==3u*FRAGMENT_SIZE);
      cache.valid=0;before=(unsigned)retro_atomic_load_relaxed_int(&bytes);
      view=xe_svod_sector(files,&cache,0,2048);CHECK(view && !memcmp(view,contents[0]+2048,2048));
      CHECK(xe_svod_sector(files,&cache,0,2048)==view);
      CHECK((unsigned)retro_atomic_load_relaxed_int(&bytes)-before==(mapping?0u:2048u));
      if(mapping) CHECK(view==contents[0]+2048);
      else {direct=out;direct_size=sizeof(out);}
      CHECK(xe_svod_files_read(files,2,77,out,64) && !memcmp(out,contents[2]+77,64));direct=NULL;
      CHECK(!xe_svod_files_read(files,3,0,out,1));CHECK(!xe_svod_files_read(files,0,FRAGMENT_SIZE-1,out,2));
      before=opens;shared=files;retro_atomic_int_init(&start,0);retro_atomic_int_init(&bad,0);
      for(i=0;i<4;++i) {threads[i]=sthread_create(reader,(void*)(uintptr_t)i);CHECK(threads[i]);}
      retro_atomic_store_release_int(&start,1);for(i=0;i<4;++i) sthread_join(threads[i]);
      CHECK(!retro_atomic_load_acquire_int(&bad) && opens==before);
      xe_svod_files_close(files);closed();CHECK(closes==3);
   }
   position_seeks=0;
   for(i=1;i<=13;++i) if(i<=5 || i==11 || i==12 || i==13) {
      configure(0);fault=i;files=xe_svod_files_open("content://svod/game",3);CHECK(!files);closed();
   }
   configure(0);files=xe_svod_files_open("content://svod/game",3);CHECK(files);
   for(i=6;i<=10;++i) {
      fault=i;retro_atomic_store_release_int(&bytes,0);memset(out,0x55,sizeof(out));
      got=xe_svod_files_transfer(files,0,100+i,out,16);
      CHECK(got==(i==7?16u:i==8?5u:0u));
   }
   fault=0;xe_svod_files_close(files);closed();
#ifdef XE_SVOD_NATIVE
   native();
#endif
   if(argc>1) native_filesystem(argv[1]);
   puts("PASS: SVOD borrowed/fallback sectors, direct reads, 16000 concurrent transfers, legacy and position-returning seeks and fault cleanup");return 0;
}

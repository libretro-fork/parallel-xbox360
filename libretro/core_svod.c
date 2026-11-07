#include "core_svod.h"
#include "core_file.h"
#include <retro_dirent.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct svod_fragment {
   union { const char *name; xe_file *file; } source;
   retro_atomic_int_t busy;
   retro_eventcount_t available;
   int gated;
} svod_fragment;
struct xe_svod_files {
   svod_fragment *fragments;
   size_t count, opened;
   uint64_t size;
};
static unsigned upper(unsigned c) {return c>='a' && c<='z'?c-32:c;}
static int compare(const void *left,const void *right) {
   const unsigned char *a=(const unsigned char *)((const svod_fragment *)left)->source.name;
   const unsigned char *b=(const unsigned char *)((const svod_fragment *)right)->source.name;
   while(*a && upper(*a)==upper(*b)) {++a;++b;}
   return upper(*a)<upper(*b)?-1:upper(*a)>upper(*b);
}
static int leaf(const char *name) {
   if(!name || !*name) return 0;
   while(*name) {if(*name=='/' || *name=='\\' || *name==':') return 0;++name;}
   return 1;
}
static int dot(const char *name) {return !strcmp(name,".") || !strcmp(name,"..");}
void xe_svod_files_close(xe_svod_files *files) {
   size_t i;
   if(!files) return;
   for(i=0;i<files->opened;++i) {
      if(files->fragments[i].gated) retro_eventcount_free(&files->fragments[i].available);
      xe_file_close(files->fragments[i].source.file);
   }
   free(files->fragments);free(files);
}
xe_svod_files *xe_svod_files_open(const char *package,size_t expected) {
   xe_svod_files *files=NULL;
   RDIR *directory=NULL;
   char *root=NULL,*names=NULL,*path=NULL;
   const char *name;
   size_t prefix,count=0,total=0,max_name=0,n,used=0,i;
   uint64_t size;
   if(!package || !*package || !expected || expected>SIZE_MAX/sizeof(svod_fragment)) return NULL;
   prefix=strlen(package);
   if(prefix>SIZE_MAX-7) return NULL;
   root=(char *)malloc(prefix+6);
   if(!root) return NULL;
   memcpy(root,package,prefix);memcpy(root+prefix,".data",6);prefix+=5;
   directory=retro_opendir_include_hidden(root,true);
   if(!directory) goto fail;
   while(retro_readdir(directory)) {
      name=retro_dirent_get_name(directory);
      if(!name) goto fail;
      if(dot(name)) continue;
      if(!leaf(name) || retro_dirent_is_dir(directory,NULL) || count==expected) goto fail;
      n=strlen(name);
      if(n==SIZE_MAX || n+1>SIZE_MAX-total) goto fail;
      total+=n+1;if(n>max_name) max_name=n;++count;
   }
   retro_closedir(directory);directory=NULL;
   if(count!=expected || max_name>SIZE_MAX-prefix-2) goto fail;
   files=(xe_svod_files *)calloc(1,sizeof(*files));
   if(!files) goto fail;
   files->fragments=(svod_fragment *)calloc(count,sizeof(*files->fragments));
   names=(char *)malloc(total);path=(char *)malloc(prefix+max_name+2);
   if(!files->fragments || !names || !path) goto fail;
   files->count=count;count=0;
   directory=retro_opendir_include_hidden(root,true);
   if(!directory) goto fail;
   while(retro_readdir(directory)) {
      name=retro_dirent_get_name(directory);
      if(!name) goto fail;
      if(dot(name)) continue;
      if(!leaf(name) || retro_dirent_is_dir(directory,NULL) || count==expected) goto fail;
      n=strlen(name);
      if(n==SIZE_MAX || n+1>total-used || n>max_name) goto fail;
      memcpy(names+used,name,n+1);files->fragments[count++].source.name=names+used;used+=n+1;
   }
   retro_closedir(directory);directory=NULL;
   if(count!=expected) goto fail;
   qsort(files->fragments,count,sizeof(*files->fragments),compare);
   for(i=1;i<count;++i) if(!compare(files->fragments+i-1,files->fragments+i)) goto fail;
   memcpy(path,root,prefix);path[prefix]='/';
   for(i=0;i<count;++i) {
      name=files->fragments[i].source.name;n=strlen(name);
      memcpy(path+prefix+1,name,n+1);
      files->fragments[i].source.file=xe_file_open(path);
      if(!files->fragments[i].source.file) goto fail;
      ++files->opened;size=xe_file_size(files->fragments[i].source.file);
      if(size>UINT64_MAX-files->size) goto fail;
      files->size+=size;
      if(size && !xe_file_view(files->fragments[i].source.file,0,0)) {
         retro_atomic_int_init(&files->fragments[i].busy,0);
         if(!retro_eventcount_init(&files->fragments[i].available)) goto fail;
         files->fragments[i].gated=1;
      }
   }
   free(root);free(names);free(path);return files;
fail:
   if(directory) retro_closedir(directory);
   free(root);free(names);free(path);xe_svod_files_close(files);return NULL;
}
uint64_t xe_svod_files_size(const xe_svod_files *files) {return files?files->size:0;}
int xe_svod_files_range(const xe_svod_files *files,size_t index,uint64_t offset,size_t length) {
   uint64_t size;
   if(!files || index>=files->count) return 0;
   size=xe_file_size(files->fragments[index].source.file);
   return offset<=size && length<=size-offset;
}
const uint8_t *xe_svod_files_view(const xe_svod_files *files,size_t index,uint64_t offset,size_t length) {
   if(!xe_svod_files_range(files,index,offset,length)) return NULL;
   return xe_file_view(files->fragments[index].source.file,offset,length);
}
static int transfer(xe_svod_files *files,size_t index,uint64_t offset,void *out,size_t length,size_t *read) {
   svod_fragment *fragment;
   int key,result;
   *read=0;
   if(!xe_svod_files_range(files,index,offset,length) || (!out && length)) return 0;
   fragment=files->fragments+index;
   if(!fragment->gated) return xe_file_read_transfer(fragment->source.file,offset,out,length,read);
   /* The old native offset-read had no shared cursor. Serialize only the
    * unmappable VFS cursor, without a C++ mutex or a polling/sleep loop.
    * This fallback is a required exclusion gate, not lock-free file I/O. */
   while(!retro_atomic_cas_int(&fragment->busy,0,1)) {
      key=retro_eventcount_prepare_wait(&fragment->available);
      if(retro_atomic_cas_int(&fragment->busy,0,1)) {
         retro_eventcount_cancel_wait(&fragment->available);break;
      }
      retro_eventcount_commit_wait(&fragment->available,key);
   }
   result=xe_file_read_transfer(fragment->source.file,offset,out,length,read);
   retro_atomic_store_release_int(&fragment->busy,0);
   retro_eventcount_notify(&fragment->available);
   return result;
}
int xe_svod_files_read(xe_svod_files *files,size_t index,uint64_t offset,void *out,size_t length) {
   size_t read;return transfer(files,index,offset,out,length,&read);
}
size_t xe_svod_files_transfer(xe_svod_files *files,size_t index,uint64_t offset,void *out,size_t length) {
   size_t read;transfer(files,index,offset,out,length,&read);return read;
}
const uint8_t *xe_svod_sector(xe_svod_files *files,xe_svod_sectors *cache,size_t index,uint64_t offset) {
   const uint8_t *view;
   unsigned slot;
   if(!cache || (offset&2047)) return NULL;
   view=xe_svod_files_view(files,index,offset,2048);
   if(view) return view;
   slot=(unsigned)(((offset>>11)^index)&3);
   if(!(cache->valid&(1u<<slot)) || cache->files[slot]!=index || cache->offsets[slot]!=offset) {
      cache->valid&=~(1u<<slot);
      if(!xe_svod_files_read(files,index,offset,cache->bytes[slot],2048)) return NULL;
      cache->files[slot]=index;cache->offsets[slot]=offset;cache->valid|=1u<<slot;
   }
   return cache->bytes[slot];
}
static uint32_t codepoint(uint8_t byte) {
   static const uint16_t special[32]={0x20AC,0x81,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,
      0x2C6,0x2030,0x160,0x2039,0x152,0x8D,0x17D,0x8F,0x90,0x2018,0x2019,0x201C,
      0x201D,0x2022,0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0x9D,0x17E,0x178};
   return byte>=0x80 && byte<0xA0?special[byte-0x80]:byte;
}
size_t xe_svod_name_size(const uint8_t *name,size_t length) {
   size_t i,total=0,n;uint32_t c;
   if(length && !name) return SIZE_MAX;
   for(i=0;i<length;++i) {c=codepoint(name[i]);n=c<0x80?1:c<0x800?2:3;
      if(n>SIZE_MAX-total) return SIZE_MAX;
      total+=n;}
   return total;
}
int xe_svod_name_write(char *out,size_t capacity,const uint8_t *name,size_t length) {
   size_t i,n,at=0;uint32_t c;
   if((length && !name) || (capacity && !out)) return 0;
   for(i=0;i<length;++i) {
      c=codepoint(name[i]);n=c<0x80?1:c<0x800?2:3;
      if(n>capacity-at) return 0;
      if(c<0x80) out[at++]=(char)c;
      else if(c<0x800) {out[at++]=(char)(0xC0|(c>>6));out[at++]=(char)(0x80|(c&63));}
      else {out[at++]=(char)(0xE0|(c>>12));out[at++]=(char)(0x80|((c>>6)&63));out[at++]=(char)(0x80|(c&63));}
   }
   return 1;
}

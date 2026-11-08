/* Native media admission/PCM oracle. No frontend, guest CPU or device. */
#include "libretro/core_media_audio.h"
#include <formats/audio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"media line %u\n",(unsigned)__LINE__);exit(1);} } while (0)
typedef struct source {
   const uint8_t *data;
   size_t size, prefix, bytes, calls;
   int short_read;
} source;
static float reference_pcm[1536];
static int64_t read_at(void *ud,uint64_t offset,uint8_t *dst,size_t bytes)
{
   source *s=(source*)ud;
   size_t at=(size_t)offset, take, tag=s->prefix ? s->prefix-10:0, i;
   uint8_t header[10]={'I','D','3',4,0,0,0,0,0,0};
   if (offset>SIZE_MAX || at>s->prefix+s->size || bytes>s->prefix+s->size-at) return -1;
   s->calls++; s->bytes+=bytes;
   if (s->short_read && bytes) bytes--;
   if (at<s->prefix) {
      take=s->prefix-at; if (take>bytes) take=bytes;
      memset(dst,0,take);
      for(i=0;i<4;i++) header[6+i]=(uint8_t)((tag>>(21-7*i))&127);
      for(i=at;i<10 && i<at+take;i++) dst[i-at]=header[i];
      at+=take; dst+=take; bytes-=take;
   }
   if (bytes) memcpy(dst,s->data+at-s->prefix,bytes);
   return s->short_read ? -1 : (int64_t)(bytes+(offset<s->prefix ? at-(size_t)offset:0));
}
static enum audio_type_enum kind(const uint8_t *p)
{
   return !memcmp(p,"RIFF",4) || !memcmp(p,"\x30\x26\xb2\x75\x8e\x66\xcf\x11",8)
      ? AUDIO_TYPE_WMA:AUDIO_TYPE_MP3;
}
static void compare(const uint8_t *data,size_t size,int window,size_t prefix)
{
   source s;
   xe_media_audio *media;
   void *reference;
   enum audio_type_enum type=kind(data);
   unsigned channels, rate, output_channels, output_rate;
   const float *pcm;
   size_t frames, got, total=0, i;
   int result;
   memset(&s,0,sizeof(s)); s.data=data; s.size=size; s.prefix=prefix;
   reference=audio_transfer_new(type); CHECK(reference);
   audio_transfer_set_buffer_ptr(reference,type,(void*)data,size);
   CHECK(audio_transfer_start(reference,type));
   CHECK(audio_transfer_info(reference,type,&channels,&rate,NULL));
   media=xe_media_audio_open(window ? NULL:data,size+prefix,read_at,&s); CHECK(media);
   CHECK(xe_media_audio_info(media,&output_rate,&output_channels));
   CHECK(output_rate==rate && output_channels==(channels==6 ? 6u:2u));
   while ((result=xe_media_audio_read(media,&pcm,&frames))>0) {
      CHECK(frames && frames*channels<=1536);
      CHECK(audio_transfer_read_f32(reference,type,reference_pcm,frames,&got)>=0 && got==frames);
      if (channels==1) {
         for(i=0;i<frames;i++) CHECK(pcm[2*i]==reference_pcm[i] && pcm[2*i+1]==reference_pcm[i]);
      } else CHECK((channels==2 || channels==6) && !memcmp(pcm,reference_pcm,frames*channels*sizeof(float)));
      total+=frames;
   }
   CHECK(result==0 && total);
   CHECK(audio_transfer_read_f32(reference,type,reference_pcm,1,&got)==AUDIO_PROCESS_END && !got);
   if (prefix) CHECK(s.bytes<size+2*65536);
   xe_media_audio_close(media); audio_transfer_free(reference,type);
   printf("PASS native float media: %u channels -> %u, %lu frames, %s, prefix %lu, read %lu bytes\n",
      channels,output_channels,(unsigned long)total,window ? "window":"borrowed",(unsigned long)prefix,(unsigned long)s.bytes);
}
int main(int argc,char **argv)
{
   FILE *f;
   uint8_t *data;
   long length;
   size_t size;
   source s;
   CHECK(argc==2); f=fopen(argv[1],"rb"); CHECK(f);
   CHECK(!fseek(f,0,SEEK_END)); length=ftell(f); CHECK(length>=16);
   CHECK(!fseek(f,0,SEEK_SET)); size=(size_t)length;
   data=(uint8_t*)malloc(size); CHECK(data && fread(data,1,size,f)==size && !fclose(f));
   compare(data,size,0,0); compare(data,size,1,0);
   if (kind(data)==AUDIO_TYPE_MP3) compare(data,size,1,4*1024*1024+10);
   memset(&s,0,sizeof(s)); s.data=data; s.size=size; s.short_read=1;
   CHECK(!xe_media_audio_open(NULL,size,read_at,&s));
   CHECK(!xe_media_audio_open((const uint8_t*)(UINTPTR_MAX-8),size,read_at,&s));
   CHECK(!xe_media_audio_open(data,0,NULL,NULL));
   free(data); return 0;
}

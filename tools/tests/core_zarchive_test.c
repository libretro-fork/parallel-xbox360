/* Real ZArchive blocks/cache regression; MSVC C89. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
void *archive_test_writer(FILE *file);
int archive_test_write(void *writer,const char *name,const void *data,size_t size);
int archive_test_mkdir(void *writer,const char *name);
void archive_test_finish(void *writer);
void *archive_test_open(const char *path);
uint64_t archive_test_read(void *reader,const char *name,uint64_t offset,size_t size,void *output);
void archive_test_close(void *reader);
static unsigned char source[5*1024*1024+11], random_bytes[131079], output[70003];
int main(int argc,char **argv){
  FILE *file;
  void *writer,*reader;
  uint32_t rng=0x12345678;
  size_t i,offset,want;
  if(argc!=2)return 2;
  for(i=0;i<sizeof(source);++i)source[i]=(unsigned char)((i%251)^(i/32768));
  for(i=0;i<sizeof(random_bytes);++i){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;random_bytes[i]=(unsigned char)rng;}
  file=fopen(argv[1],"wb");if(!file)return 3;
  writer=archive_test_writer(file);if(!writer)return 4;
  if(!archive_test_mkdir(writer,"alpha"))return 11;
  if(!archive_test_write(writer,"alpha/data.bin",source,sizeof(source)) ||
     !archive_test_write(writer,"random.bin",random_bytes,sizeof(random_bytes)) ||
     !archive_test_write(writer,"empty.bin",source,0))return 5;
  archive_test_finish(writer);if(ferror(file)||fclose(file))return 6;
  reader=archive_test_open(argv[1]);if(!reader)return 7;
  /* Reverse traversal exceeds the four-MiB cache and crosses block edges. */
  for(i=0;i<120;++i){offset=(sizeof(source)-1-(i*65521u)%sizeof(source));want=sizeof(source)-offset;if(want>sizeof(output))want=sizeof(output);
    if(archive_test_read(reader,"alpha/data.bin",offset,want,output)!=want||memcmp(output,source+offset,want))return 8;
  }
  for(offset=0;offset<sizeof(random_bytes);offset+=32761){want=sizeof(random_bytes)-offset;if(want>sizeof(output))want=sizeof(output);
    if(archive_test_read(reader,"random.bin",offset,want,output)!=want||memcmp(output,random_bytes+offset,want))return 9;
  }
  if(archive_test_read(reader,"empty.bin",0,1,output)!=0||archive_test_read(reader,"absent",0,1,output)!=0)return 10;
  archive_test_close(reader);puts("PASS: real archive compressed/raw blocks, cross-block random reads, cache eviction and empty files");return 0;
}

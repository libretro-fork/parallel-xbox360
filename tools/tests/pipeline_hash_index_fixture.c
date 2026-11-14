/* Real parent/candidate owners in one process. MSVC C89, Windows test only.
 * Correctness/counting and uninstrumented timing are separate builds/runs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "pipeline_hash_index_fixture.h"
enum{MAX_OWNERS=32768,LOOKUPS=65536};
static void *owners[2][MAX_OWNERS],*queries[2][MAX_OWNERS];
static uint64_t hashes[MAX_OWNERS];
static volatile uintptr_t checksum;
static double now(void)
{
 LARGE_INTEGER n,f;
 if(!QueryPerformanceCounter(&n) || !QueryPerformanceFrequency(&f))ExitProcess(2);
 return (double)n.QuadPart/(double)f.QuadPart;
}
static uint64_t key_hash(unsigned i,unsigned mode)
{
 if(mode==1)return ((uint64_t)i<<32)|42;
 if(mode==2)return 0;
 return (uint64_t)i*UINT64_C(0x9e3779b97f4a7c15);
}
static const xe_pipeline_index_fixture *load_api(const char *path,HMODULE *module)
{
 xe_pipeline_index_fixture_get get;
 const xe_pipeline_index_fixture *api;
 *module=LoadLibraryA(path);if(!*module)return NULL;
 get=(xe_pipeline_index_fixture_get)(void*)GetProcAddress(*module,"xe_pipeline_index_fixture_api");
 if(!get)return NULL;
 api=get();
 return api && api->version==1 && api->bytes==sizeof(*api)?api:NULL;
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"native pipeline index line%u: %s\n",(unsigned)__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv)
{
 static const unsigned counts[4]={64,1024,8192,32768};
 const xe_pipeline_index_fixture *api[2];HMODULE modules[2]={0};void *contexts[2],*modified;
 xe_pipeline_index_key key;FILE *csv=NULL;unsigned c,mode,n,i,j,role,k,trial,miss,profiles=0;
 size_t allocations[2];double started,elapsed;void **input;uintptr_t sum;
 CHECK(argc==3 || argc==4);
 api[0]=load_api(argv[1],&modules[0]);api[1]=load_api(argv[2],&modules[1]);
 CHECK(api[0] && api[1] && api[0]->intrusive==0 && api[1]->intrusive==1);
 CHECK(api[0]->tracked_allocations==api[1]->tracked_allocations);
 if(argc==4){CHECK(!api[0]->tracked_allocations);csv=fopen(argv[3],"wb");CHECK(csv);
  CHECK(fprintf(csv,"profile,owners,hash_mode,miss,trial,role,lookups,seconds\n")>0);}
 printf("actual native owner bytes parent%zu candidate%zu\n",api[0]->owner_bytes,api[1]->owner_bytes);
 for(c=0;c<4;++c)for(mode=0;mode<3;++mode){
  n=counts[c];if((mode==2 && n>1024) || (mode==1 && n>8192))continue;
  for(role=0;role<2;++role){
   contexts[role]=api[role]->open();CHECK(contexts[role]);CHECK(!api[role]->count(contexts[role]));
   for(i=0;i<n;++i){
    for(k=0;k<8;++k)key.words[k]=((uint64_t)i+1)*(k+1)*UINT64_C(0x123456789abcdef);
    owners[role][i]=api[role]->owner_new(&key);CHECK(owners[role][i]);
    key.words[7]^=UINT64_C(0x8000000000000000);
    queries[role][i]=api[role]->owner_new(&key);CHECK(queries[role][i]);hashes[i]=key_hash(i,mode);
   }
   api[role]->allocation_reset();
   for(i=0;i<n;++i)CHECK(api[role]->insert(contexts[role],hashes[i],owners[role][i]));
   allocations[role]=api[role]->allocation_count();CHECK(api[role]->count(contexts[role])==n);
   if(api[role]->tracked_allocations)CHECK(role?allocations[role]==0:allocations[role]>=n);
   for(i=0;i<n;++i){
    CHECK(api[role]->find(contexts[role],hashes[i],owners[role][i])==owners[role][i]);
    CHECK(!api[role]->find(contexts[role],hashes[i],queries[role][i]));
   }
   /* Independent byte sensitivity, including fields beyond the fast prefix. */
   for(j=0;j<sizeof(key);++j){
    for(k=0;k<8;++k)key.words[k]=(k+1)*UINT64_C(0x123456789abcdef);
    ((unsigned char*)&key)[j]^=1;
    modified=api[role]->owner_new(&key);CHECK(modified);
    CHECK(!api[role]->find(contexts[role],hashes[0],modified));api[role]->owner_free(modified);
   }
  }
  if(api[0]->tracked_allocations)
   printf("profile%u owners%u hashmode%u bookkeeping_allocations%zu/%zu bucket_bytes%zu/%zu\n",profiles,n,mode,
    allocations[0],allocations[1],api[0]->bucket_bytes(contexts[0]),api[1]->bucket_bytes(contexts[1]));
  else printf("profile%u owners%u hashmode%u allocations_untracked bucket_bytes%zu/%zu\n",profiles,n,mode,
   api[0]->bucket_bytes(contexts[0]),api[1]->bucket_bytes(contexts[1]));
  if(csv)for(miss=0;miss<2;++miss)for(trial=0;trial<7;++trial)for(j=0;j<2;++j){
   role=j^(trial&1);input=miss?queries[role]:owners[role];sum=0;
   started=now();
   for(i=0;i<LOOKUPS;++i){k=(i*1664525u+1013904223u)&(n-1);
    sum^=(uintptr_t)api[role]->find(contexts[role],hashes[k],input[k]);}
   elapsed=now()-started;checksum^=sum;
   CHECK(fprintf(csv,"%u,%u,%u,%u,%u,%s,%u,%.12f\n",profiles,n,mode,miss,trial,
    role?"candidate":"parent",LOOKUPS,elapsed)>0);
  }
  for(role=0;role<2;++role){
   api[role]->clear(contexts[role]);CHECK(!api[role]->count(contexts[role]));
   for(i=0;i<n;++i)CHECK(!api[role]->find(contexts[role],hashes[i],owners[role][i]));
   /* Reinsert the same addresses after drain, then clear before freeing them. */
   for(i=0;i<n;++i)CHECK(api[role]->insert(contexts[role],hashes[i],owners[role][i]));
   CHECK(api[role]->count(contexts[role])==n);api[role]->close(contexts[role]);
   for(i=0;i<n;++i){api[role]->owner_free(owners[role][i]);api[role]->owner_free(queries[role][i]);}
   if(api[role]->tracked_allocations)CHECK(!api[role]->live_allocation_count());
  }
  ++profiles;
 }
 if(csv)CHECK(!fclose(csv));CHECK(FreeLibrary(modules[1]));CHECK(FreeLibrary(modules[0]));
 printf("PASS: %u native parent/candidate key profiles, colliding full keys, same-address reinsertion and independent modules\n",profiles);
 return 0;
}

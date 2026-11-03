/* Fail parser allocations in isolation; the owned input must be freed once. */
#include <formats/rxml.h>
#include "libretro/core_xlast.h"
#include "core_xlast_fixture.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t calls,fail_at,live,input_frees;
static void *input;
void *xe_rxml_malloc(size_t size) {
  void *p;
  if(++calls==fail_at) return NULL;
  p=malloc(size);
  if(p) { ++live; memset(p,0xa5,size); }
  return p;
}
void *xe_rxml_calloc(size_t count,size_t size) {
  void *p;
  if(++calls==fail_at) return NULL;
  p=calloc(count,size); if(p) ++live; return p;
}
void *xe_rxml_realloc(void *old,size_t size) {
  void *p;
  int was_null=old==NULL;
  if(++calls==fail_at) return NULL;
  p=realloc(old,size); if(p && was_null) ++live; return p;
}
void xe_rxml_free(void *p) {
  if(p) {
    if(p==input && !input_frees) ++input_frees;
    else { assert(live); --live; }
  }
  free(p);
}
static void exercise(size_t nth,int incremental) {
  static const char xml[]="<root a='x&amp;y'><child>one</child><child><![CDATA[two]]></child></root>";
  rxml_document_t *doc;
  rxml_parse_t *parse;
  int state;
  input=malloc(sizeof(xml)); assert(input); memcpy(input,xml,sizeof(xml));
  calls=0; live=0; input_frees=0; fail_at=nth;
  if(incremental) {
    parse=rxml_parse_begin_owned((char *)input,sizeof(xml)-1,RXML_OPT_STRICT_EOF);
    if(parse) {
      do { state=rxml_parse_step(parse,3); } while(!state);
      doc=rxml_parse_end(parse); rxml_free_document(doc);
    }
  } else {
    doc=rxml_load_document_owned_opts((char *)input,sizeof(xml)-1,RXML_OPT_STRICT_EOF,NULL);
    rxml_free_document(doc);
  }
  assert(!live); assert(input_frees==1);
}
static void exercise_xlast(size_t nth,unsigned encoding) {
  static const char xml[]="<XboxLiveSubmissionProject><GameConfigProject titleName='x'/></XboxLiveSubmissionProject>";
  xe_xlast *doc;
  size_t len=strlen(xml),i;
  unsigned char *bytes;
  input=malloc(encoding?len*2+3:len+1); assert(input);
  if(encoding) {
    bytes=(unsigned char *)input; bytes[0]=0xff; bytes[1]=0xfe;
    for(i=0;i<len;++i) { bytes[2+i*2]=(unsigned char)xml[i]; bytes[3+i*2]=0; }
    len=len*2+2; bytes[len]=0;
  } else memcpy(input,xml,len+1);
  calls=0; live=0; input_frees=0; fail_at=nth;
  doc=xe_xlast_parse_owned((char *)input,len); xe_xlast_destroy(doc);
  assert(!live && input_frees==1);
  input=NULL; calls=0; live=0; input_frees=0; fail_at=nth;
  doc=xe_xlast_create_gzip(xlast_gzip,sizeof(xlast_gzip),XE_XLAST_FIXTURE_SIZE);
  xe_xlast_destroy(doc); assert(!live && !input_frees);
}
static void exercise_index(size_t nth) {
  static const char begin[]="<XboxLiveSubmissionProject><GameConfigProject><LocalizedStrings>";
  static const char end[]="</LocalizedStrings></GameConfigProject></XboxLiveSubmissionProject>";
  char *xml=(char *)malloc(32768);
  size_t len;
  unsigned i;
  xe_xlast *doc;
  assert(xml); strcpy(xml,begin); len=strlen(xml);
  for(i=0;i<128;++i) len+=(size_t)sprintf(xml+len,
    "<LocalizedString id='%u'><Text locale='en-US'>x</Text></LocalizedString>",i);
  strcpy(xml+len,end); len+=strlen(end);
  input=xml; calls=0; live=0; input_frees=0; fail_at=nth;
  doc=xe_xlast_parse_owned(xml,len);
  if(doc) assert(!strcmp(xe_xlast_localized(doc,127,1),"x"));
  xe_xlast_destroy(doc); assert(!live && input_frees==1);
}
static void exercise_growth(size_t nth,int incremental)
{
   char *xml=(char *)malloc(32768);
   char *at=xml;
   rxml_document_t *doc=NULL;
   rxml_parse_t *parse;
   unsigned i;
   int state;
   assert(xml);
   for(i=0;i<96;++i) { memcpy(at,"<n>",3); at+=3; }
   for(i=0;i<2000;++i) { memcpy(at,"a&amp;",6); at+=6; }
   for(i=0;i<96;++i) { memcpy(at,"</n>",4); at+=4; }
   *at=0;
   input=xml; calls=live=input_frees=0; fail_at=nth;
   if(incremental) {
      parse=rxml_parse_begin_owned(xml,(size_t)(at-xml),RXML_OPT_STRICT_EOF);
      if(parse) {
         do { state=rxml_parse_step(parse,17); } while(!state);
         doc=rxml_parse_end(parse);
      }
   } else doc=rxml_load_document_owned_opts(xml,(size_t)(at-xml),RXML_OPT_STRICT_EOF,NULL);
   if(!nth) {
      struct rxml_node *node;
      assert(doc); node=rxml_root_node(doc);
      for(i=1;i<96;++i) { assert(node); node=node->children; }
      assert(node && node->data && strlen(node->data)==4000);
   }
   rxml_free_document(doc); assert(!live && input_frees==1);
}
int main(int argc,char **argv) {
  size_t i;
  if(argc>1) exercise((size_t)strtoul(argv[1],NULL,10),0);
  else for(i=1;i<=32;++i) { exercise(i,0); exercise(i,1); exercise_xlast(i,0); exercise_xlast(i,1); exercise_index(i); }
  for(i=0;i<=64;++i) { exercise_growth(i,0); exercise_growth(i,1); }
  puts("rxml allocation failures release input and parser allocations: PASS");
  return 0;
}

#include "libretro/core_xlast.h"
#include "core_xlast_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *xe_pugi_open(const void *,size_t);
void xe_pugi_close(void *);
const char *xe_pugi_attr(void *,const char *,const char *);
const char *xe_pugi_text(void *,const char *);
#define PROJECT "/XboxLiveSubmissionProject/GameConfigProject"
static const char prefix[]="<?xml version='1.0'?><XboxLiveSubmissionProject><GameConfigProject titleName='Game &amp; title'>"
 "<ProductInformation offlinePlayersMax='2' publisherStringId='1'/><LocalizedStrings>"
 "<SupportedLocale locale='en-US'/><SupportedLocale locale='ja-JP'/><SupportedLocale locale='invalid'/>"
 "<LocalizedString id='1'><Text locale='en-US'>";
static const char suffix1[]="</Text><Text locale='ja-JP'>&#x65E5;</Text></LocalizedString></LocalizedStrings>"
 "<Presence><PresenceMode contextValue='5' stringId='1'/></Presence>"
 "<Properties><Property id='0x00000012' stringId='2'/></Properties>"
 "<Contexts><Context id='0xDEADBEEF'><ContextValue value='7' stringId='3'/></Context></Contexts>"
;
static const char suffix2[]="<Matchmaking><Queries><Query id='7' friendlyName='Q &amp; A'><Returns><Value id='0x20'/></Returns>"
 "<Parameters><Value id='10'/></Parameters><Filters><Value left='B'/></Filters></Query></Queries></Matchmaking>"
 "</GameConfigProject></XboxLiveSubmissionProject>";
struct values { uint32_t data[32]; size_t count; };
static void sink(void *opaque,uint32_t value) {
  struct values *p=(struct values *)opaque;
  assert(p->count<32); p->data[p->count++]=value;
}
static void wide_unit(unsigned char *p,size_t *at,unsigned width,int little,uint32_t value) {
  unsigned j;
  for(j=0;j<width;++j) p[(*at)++]=(unsigned char)(value>>(8*(little?j:width-1-j)));
}
static char *encode(const char *xml,size_t *size,unsigned encoding) {
  size_t len=strlen(xml),i=0,at=0;
  unsigned width=encoding<2?1:encoding==2||encoding==3||encoding==6||encoding==7?2:4;
  int little=encoding==2||encoding==4||encoding==6||encoding==8;
  unsigned char *p=(unsigned char *)malloc(len*width+5);
  assert(p);
  if(encoding==1) { p[at++]=0xef; p[at++]=0xbb; p[at++]=0xbf; }
  else if(encoding>=2 && encoding<=5) wide_unit(p,&at,width,little,0xfeff);
  while(i<len) {
    uint32_t ch=(unsigned char)xml[i++];
    unsigned extra,j;
    if(width==1) { p[at++]=(unsigned char)ch; continue; }
    if(ch>=0x80) {
      extra=ch<0xe0?1:ch<0xf0?2:3;
      ch&=extra==1?31:extra==2?15:7;
      assert(i+extra<=len);
      for(j=0;j<extra;++j) ch=(ch<<6)|((unsigned char)xml[i++]&63);
    }
    if(width==2 && ch>0xffff) {
      ch-=0x10000; wide_unit(p,&at,width,little,0xd800+(ch>>10));
      ch=0xdc00+(ch&1023);
    }
    wide_unit(p,&at,width,little,ch);
  }
  p[at]=0; *size=at; return (char *)p;
}
static void check(const char *body,unsigned encoding) {
  size_t len=sizeof(prefix)-1+strlen(body)+sizeof(suffix1)-1+sizeof(suffix2),encoded;
  char *xml=(char *)malloc(len),*input;
  void *reference;
  xe_xlast *doc;
  const xe_xlast_query *query;
  struct values v;
  uint32_t fields[7],value=0;
  assert(xml); strcpy(xml,prefix); strcat(xml,body); strcat(xml,suffix1); strcat(xml,suffix2);
  input=encode(xml,&encoded,encoding); free(xml);
  reference=xe_pugi_open(input,encoded); assert(reference);
  doc=xe_xlast_parse_owned(input,encoded);
  if(!doc) fprintf(stderr,"rxml rejected encoding %u body %s\n",encoding,body);
  assert(doc);
  assert(!strcmp(xe_xlast_title(doc),xe_pugi_attr(reference,PROJECT,"titleName")));
  assert(xe_xlast_product(doc,fields)==9 && fields[0]==2 && fields[3]==1);
  assert(!strcmp(xe_xlast_localized(doc,1,1),xe_pugi_text(reference,PROJECT "/LocalizedStrings/LocalizedString[@id='1']/Text[@locale='en-US']")));
  assert(!strcmp(xe_xlast_localized(doc,1,2),xe_pugi_text(reference,PROJECT "/LocalizedStrings/LocalizedString[@id='1']/Text[@locale='ja-JP']")));
  assert(!strcmp(xe_xlast_localized(doc,1,99),xe_xlast_localized(doc,1,1)));
  assert(!*xe_xlast_localized(doc,999,1));
  v.count=0; assert(xe_xlast_languages(doc,sink,&v)==2);
  assert(v.count==2 && v.data[0]==1 && v.data[1]==2);
  assert(xe_xlast_presence(doc,5,&value) && value==1); assert(!xe_xlast_presence(doc,6,&value));
  assert(xe_xlast_property(doc,0x12,&value) && value==2);
  assert(xe_xlast_context(doc,0xdeadbeefU,7,&value) && value==3);
  query=xe_xlast_find_query(doc,7); assert(query);
  assert(!strcmp(xe_xlast_query_name(query),xe_pugi_attr(reference,PROJECT "/Matchmaking/Queries/Query[@id='7']","friendlyName")));
  v.count=0; assert(xe_xlast_query_values(query,0,sink,&v)==1 && v.data[0]==32);
  v.count=0; assert(xe_xlast_query_values(query,1,sink,&v)==1 && v.data[0]==16);
  v.count=0; assert(xe_xlast_query_values(query,2,sink,&v)==1 && v.data[0]==11);
  xe_xlast_destroy(doc); xe_pugi_close(reference);
}
static void gzip_and_errors(void) {
  xe_xlast *doc;
  unsigned char broken[sizeof(xlast_gzip)];
  size_t i;
  char *input;
  unsigned char *bytes;
  doc=xe_xlast_create_gzip(xlast_gzip,sizeof(xlast_gzip),XE_XLAST_FIXTURE_SIZE);
  assert(doc && !strcmp(xe_xlast_localized(doc,1,1),"A&B")); xe_xlast_destroy(doc);
  for(i=0;i<sizeof(xlast_gzip);++i)
    assert(!xe_xlast_create_gzip(xlast_gzip,i,XE_XLAST_FIXTURE_SIZE));
  assert(!xe_xlast_create_gzip(xlast_gzip,sizeof(xlast_gzip),XE_XLAST_FIXTURE_SIZE-1));
  assert(!xe_xlast_create_gzip(xlast_gzip,sizeof(xlast_gzip),XE_XLAST_FIXTURE_SIZE+1));
  memcpy(broken,xlast_gzip,sizeof(broken)); broken[sizeof(broken)-8]^=1;
  assert(!xe_xlast_create_gzip(broken,sizeof(broken),XE_XLAST_FIXTURE_SIZE));
  for(i=0;i<strlen(prefix);++i) {
    input=(char *)malloc(i+1); assert(input); memcpy(input,prefix,i); input[i]=0;
    assert(!xe_xlast_parse_owned(input,i));
  }
  input=(char *)malloc(4); assert(input);
  bytes=(unsigned char *)input; bytes[0]=0xff; bytes[1]=0xfe;
  input[2]='<'; input[3]=0;
  assert(!xe_xlast_parse_owned(input,3));
  xe_xlast_destroy(NULL);
  assert(!*xe_xlast_title(NULL)); assert(!xe_xlast_find_query(NULL,0));
}
static void indexed_and_invalid(void) {
  static const char begin[]="<XboxLiveSubmissionProject><GameConfigProject><LocalizedStrings>";
  static const char end[]="</LocalizedStrings></GameConfigProject></XboxLiveSubmissionProject>";
  static const unsigned char invalid[][4]={
    {0x80,0,0,0},{0xc0,0xaf,0,0},{0xe0,0x80,0xaf,0},{0xed,0xa0,0x80,0},
    {0xf4,0x90,0x80,0x80},{0xf0,0x80,0x80,0xaf},{0xf5,0x80,0x80,0x80}
  };
  static const unsigned sizes[]={1,2,3,3,4,4,4};
  char *xml=(char *)malloc(65536),path[200],*copy;
  size_t len;
  unsigned i;
  void *ref;
  xe_xlast *doc;
  assert(xml); strcpy(xml,begin);
  strcat(xml,"<LocalizedString id='007'><Text locale='en-US'>wrong</Text></LocalizedString>");
  strcat(xml,"<LocalizedString id='0x12'><Text locale='en-US'>wrong</Text></LocalizedString>");
  strcat(xml,"<LocalizedString id='-1'><Text locale='en-US'>wrong</Text></LocalizedString>");
  strcat(xml,"<LocalizedString id='4294967296'><Text locale='en-US'>wrong</Text></LocalizedString>");
  len=strlen(xml);
  for(i=0;i<200;++i) len+=(size_t)sprintf(xml+len,
    "<LocalizedString id='%u'><Text locale='en-US'>Item%u</Text></LocalizedString>",i,i);
  strcpy(xml+len,"<LocalizedString id='7'><Text locale='en-US'>later</Text></LocalizedString>"); len=strlen(xml);
  strcpy(xml+len,"<LocalizedString id='4294967295'><Text locale='en-US'>max</Text></LocalizedString>"); len=strlen(xml);
  strcpy(xml+len,end); len+=strlen(end);
  ref=xe_pugi_open(xml,len); assert(ref); doc=xe_xlast_parse_owned(xml,len); assert(doc);
  for(i=0;i<200;++i) {
    sprintf(path,PROJECT "/LocalizedStrings/LocalizedString[@id='%u']/Text[@locale='en-US']",i);
    assert(!strcmp(xe_xlast_localized(doc,i,1),xe_pugi_text(ref,path)));
  }
  assert(!*xe_xlast_localized(doc,201,1)); assert(!strcmp(xe_xlast_localized(doc,0xffffffffU,1),"max"));
  xe_xlast_destroy(doc); xe_pugi_close(ref);
  for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
    len=7+sizes[i]; copy=(char *)malloc(len+1); assert(copy);
    memcpy(copy,"<x>",3); memcpy(copy+3,invalid[i],sizes[i]); memcpy(copy+3+sizes[i],"</x>",5);
    assert(!xe_xlast_parse_owned(copy,len));
  }
}
int main(int argc,char **argv) {
  static const char *const bodies[]={"A&amp;B","<![CDATA[A]]>B","A<!--x-->B","A<b/>B",
    "<b/>B"," <![CDATA[ ]]><b/>B","<b/><![CDATA[]]>B"," \t","<![CDATA[ \t]]>",
    "&#xE000;","&#x10FFFF;","&#1114111;","&#00000000000065;","A\r\nB"};
  unsigned i;
  if(argc>1) check(argv[1],0);
  else {
    for(i=0;i<10;++i) {
      check(bodies[0],i); check("\xe6\x97\xa5\xf0\x9f\x99\x82\xee\x80\x80",i);
    }
    for(i=0;i<sizeof(bodies)/sizeof(bodies[0]);++i) check(bodies[i],0);
  }
  gzip_and_errors(); indexed_and_invalid();
  puts("XLast parser/query encoding and pugixml differential: PASS");
  return 0;
}

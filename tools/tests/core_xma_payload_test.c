/* Independent bit oracle for borrowed XMA payloads. MSVC C89. */
#include "libretro/core_xma_payload.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned char first[2044],second[2044];
static unsigned char actual[XE_XMA_PACKET_CAPACITY+16],expected[sizeof(actual)];
static unsigned cases;
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"payload line %u\n",(unsigned)__LINE__);abort();}} while(0)
static unsigned bit(uint32_t position) {
 uint32_t byte=position/8;
 unsigned char value=byte<sizeof(first)?first[byte]:second[byte-sizeof(first)];
 return (value>>(7-position%8))&1;
}
static void check_packet(uint32_t offset,uint32_t bits) {
 xe_xma_payload_view view={first,second,sizeof(first),sizeof(second)};
 xe_xma_packet_span span;
 uint32_t i,padding=offset&7,size=1+(padding+bits+7)/8,tail;
 memset(actual,0x5a,sizeof(actual));memset(expected,0x5a,sizeof(expected));
 memset(expected,0,size);tail=(size-1)*8-padding-bits;
 expected[0]=(unsigned char)((padding<<5)|(tail<<2));
 for(i=0;i<bits;++i) if(bit(offset+i))
  expected[1+(padding+i)/8]|=(unsigned char)(1u<<(7-(padding+i)%8));
 CHECK(xe_xma_payload_packet(&view,offset,bits,actual,XE_XMA_PACKET_CAPACITY,&span));
 CHECK(span.size==size && span.header==expected[0]);
 CHECK(!memcmp(actual,expected,sizeof(actual)));++cases;
}
int main(void) {
 xe_xma_payload_view view={first,second,sizeof(first),sizeof(second)},bad;
 xe_xma_packet_span span;
 uint32_t i,j,offset,length,want,total=(sizeof(first)+sizeof(second))*8;
 for(i=0;i<sizeof(first);++i) {first[i]=(unsigned char)(i*73u+19u);second[i]=(unsigned char)(i*131u+97u);}
 /* Every valid length-header position, including all split bit positions. */
 for(offset=0;offset+15<=total;++offset) {
  want=0;for(i=0;i<15;++i) want=(want<<1)|bit(offset+i);
  CHECK(xe_xma_payload_length(&view,offset,&length) && length==want);
 }
 for(i=15;i<=255;++i) for(j=0;j<8;++j) {
  check_packet(j,i);
  check_packet((uint32_t)sizeof(first)*8-16+j,i);
 }
 for(i=0;i<8;++i) {
  check_packet(i,total-i);
  check_packet(i,16352);
  check_packet(total-15-i,15);
 }
 /* One-span input must not access absent continuation storage. */
 view.second=NULL;view.second_size=0;
 CHECK(xe_xma_payload_packet(&view,0,16352,actual,sizeof(actual),&span));
 memset(actual,0x5a,sizeof(actual));memcpy(expected,actual,sizeof(actual));
 span.size=123;span.header=99;
 CHECK(!xe_xma_payload_packet(&view,16350,15,actual,sizeof(actual),&span));
 CHECK(!xe_xma_payload_packet(&view,UINT32_MAX,15,actual,sizeof(actual),&span));
 CHECK(!xe_xma_payload_packet(&view,0,15,actual,2,&span));
 CHECK(!xe_xma_payload_packet(&view,0,14,actual,sizeof(actual),&span));
 CHECK(!xe_xma_payload_packet(&view,0,15,NULL,sizeof(actual),&span));
 CHECK(!xe_xma_payload_packet(&view,0,15,actual,sizeof(actual),NULL));
 bad=view;bad.first=NULL;CHECK(!xe_xma_payload_packet(&bad,0,15,actual,sizeof(actual),&span));
 bad=view;bad.first_size=UINT32_MAX;CHECK(!xe_xma_payload_packet(&bad,0,15,actual,sizeof(actual),&span));
 length=0xdead;CHECK(!xe_xma_payload_length(&view,16350,&length));CHECK(length==0xdead);
 CHECK(span.size==123 && span.header==99 && !memcmp(actual,expected,sizeof(actual)));
 printf("PASS: %u payload gathers, all %u length-header offsets, invalid-state nonmutation\n",cases,total-14);
 return 0;
}

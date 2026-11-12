/* Identity eligibility, final packet writes and shape ownership. BSD; C89. */
#include "transfer_identity_cases.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"identity line%d\n",__LINE__);exit(1);}}while(0)
static void classes(void)
{
 static const int family[16]={0,1,2,3,-1,-1,-1,-1,-1,-1,2,-1,3,-1,-1,-1};
 unsigned source,dest,gamma,view,wanted;int valid;
 for(source=0;source<20;++source)for(dest=0;dest<20;++dest)for(gamma=0;gamma<3;++gamma) {
  valid=gamma<2 && source<16 && dest<16 && family[source]>=0 && family[source]==family[dest];
  view=71;CHECK(xe_transfer_identity_class(source,dest,gamma,&view)==valid);
  if(valid) {
   wanted=family[dest]==1?(gamma?1:0):(unsigned)family[dest];
   CHECK(view==wanted);
  } else CHECK(view==71);
 }
 CHECK(!xe_transfer_identity_class(0,0,0,NULL));
}
static void packets(void)
{
 static const unsigned formats[4]={0,1,2,3};
 xe_transfer_address_key key,bad;
 xe_transfer_identity_options options,invalid;
 xe_transfer_identity_packet packet,saved;
 xe_transfer_identity_packet_source writer;
 xe_transfer_identity_shape shape;
 unsigned n,d,s,c,t,m,v,sample,i,tests=0;uint32_t physical;
 for(n=0;n<2;++n)for(d=0;d<3;++d)for(s=0;s<3;++s)for(c=0;c<4;++c)
 for(t=0;t<4;++t)for(m=0;m<(d?2u:1u);++m)for(v=0;v<4;++v) {
  identity_case(&key,&options,n,d,s,c,t,m);options.view=v;
  for(sample=0;sample<(1u<<d);++sample) {
   physical=d==1 && !n?sample*3:sample;
   memset(&packet,0xa5,sizeof(packet));saved=packet;
   CHECK(xe_transfer_identity_packet_prepare(&packet,&options,&key,formats[v],formats[v],v==1,physical));
   CHECK(packet.sample==physical && packet.dest_pitch==17 &&
    packet.source_pitch==31 && packet.base_delta==UINT32_C(0xfffffff9));
   writer.options=&options;writer.key=&key;writer.source_format=writer.dest_format=formats[v];
   writer.gamma16=v==1;writer.sample=physical;
   CHECK(xe_transfer_identity_packet_write(&saved,sizeof(saved),&writer));
   CHECK(!memcmp(&saved,&packet,sizeof(packet)));
   CHECK(!xe_transfer_identity_packet_write(&packet,sizeof(packet)-1,&writer));
   CHECK(!memcmp(&saved,&packet,sizeof(packet)));
   CHECK(xe_transfer_identity_shape_prepare(&shape,&options,&key));
   for(i=0;i<XE_ADDR_WORDS;++i) {
    int dynamic=i==18 || i==19 || i==20 || i==21;
    CHECK(((shape.mask>>i)&1)==(unsigned)!dynamic);
    if(dynamic)CHECK(xe_transfer_identity_parameter_word(i)==i-18);
   }
   bad=key;bad.source_pitch=0;
   CHECK(!xe_transfer_identity_packet_prepare(&packet,&options,&bad,formats[v],formats[v],v==1,physical));
   CHECK(!memcmp(&saved,&packet,sizeof(packet)));
   bad=key;bad.source_wide=1;
   CHECK(!xe_transfer_identity_packet_prepare(&packet,&options,&bad,formats[v],formats[v],v==1,physical));
   CHECK(!memcmp(&saved,&packet,sizeof(packet)));
   invalid=options;invalid.view=(v+1)%4;
   CHECK(!xe_transfer_identity_packet_prepare(&packet,&invalid,&key,formats[v],formats[v],v==1,physical));
   CHECK(!memcmp(&saved,&packet,sizeof(packet)));
   CHECK(!xe_transfer_identity_packet_prepare(&packet,&options,&key,formats[v],formats[v],v==1,4));
   CHECK(!memcmp(&saved,&packet,sizeof(packet)));++tests;
  }
 }
 CHECK(!xe_transfer_identity_packet_write(NULL,0,NULL));
 printf("PASS: %u checked final packets, sample/scale/view modes, exact shape masks and unchanged rejection\n",tests);
}
int main(void)
{
 static const unsigned emulated[8]={0,1,0,1,6,7,6,7};
 unsigned i;
 for(i=0;i<8;++i) {
  CHECK(xe_transfer_identity_samples(i,0)==emulated[i]);
  CHECK(xe_transfer_identity_samples(i,1)==i);
 }
 CHECK(!xe_transfer_identity_samples(7,2));
 classes();packets();puts("PASS: physical sample policies, exact format aliases, separate gamma identity and rejection of raw/depth/width conversions");return 0;
}

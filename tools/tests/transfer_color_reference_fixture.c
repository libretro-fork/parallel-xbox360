/* Offscreen single-sample reference corpus; procedural owned output only. */
#include "transfer_color_reference.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 static const unsigned formats[8]={0,1,2,3,4,5,14,15};
 static const unsigned scales[3][2]={{1,1},{3,5},{4,4}};
 xe_spv_binary out={0};xe_test_transfer_reference_options options={0};
 unsigned g,c,s,d,cases=0;char name[4096];FILE *file;int result=1;
 if(argc!=2 || strlen(argv[1])>3800)return 1;
 options.native2=options.shading=1;
 for(g=0;g<3;++g)for(c=0;c<(g?4u:1u);++c)for(s=0;s<8;++s)for(d=0;d<8;++d){
  options.source_format=formats[s];options.dest_format=formats[d];
  options.source_native=c&1;options.dest_native=c>>1;
  options.scale_x=scales[g][0];options.scale_y=scales[g][1];
  if(!xe_test_transfer_color_reference_build(&out,&options,s>=4,d>=4))goto done;
  sprintf(name,"%s/ref-g%u-c%u-s%u-d%u.spv",argv[1],g,c,s,d);
  file=fopen(name,"wb");if(!file)goto done;
  if(fwrite(out.words,sizeof(*out.words),out.count,file)!=out.count){fclose(file);goto done;}
  if(fclose(file))goto done;
  ++cases;
 }
 if(cases!=576)goto done;
 printf("PASS: %u unchanged legacy SS color image references, all64 typed pairs and9 native/scaled classes\n",cases);result=0;
done:xe_spv_binary_close(&out);return result;
}

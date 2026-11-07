/* Offline C89 frontend: no game, native window, renderer or audio device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro/libretro.h"
#include "libretro/libretro_vulkan.h"
extern int xe_load_probe_clean(void);
extern int xe_load_probe_seed_patches(void);
extern int xe_load_probe_pending(void);
extern int xe_load_probe_d3d12(void);
extern int xe_load_probe_providers(void);
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"load line %d\n",__LINE__); exit(1); } } while (0)
static const char *directory;
static unsigned mode,pixels,hardware,negotiations,modern,legacy,api;
static struct retro_disk_control_ext_callback disc;
static size_t RETRO_CALLCONV batch(const float *p,size_t n) { (void)p;return n; }
static bool RETRO_CALLCONV environment(unsigned command,void *data)
{
   struct retro_variable *variable;
   struct retro_hw_render_context_negotiation_interface *request;
   switch(command) {
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data=directory;return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data=2;return true;
      case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data=RETRO_LANGUAGE_ENGLISH;return true;
      case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION: *(unsigned *)data=1;return true;
      case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
         disc=*(struct retro_disk_control_ext_callback *)data;return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
         variable=(struct retro_variable *)data;
         if(!strcmp(variable->key,"xenia_graphics_api")) { variable->value=api?"d3d12":"vulkan";return true; }
         return false;
      case RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_FLOAT:
         ((struct retro_audio_sample_float_callback *)data)->batch=batch;return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         CHECK(!hardware);++pixels;return mode!=0;
      case RETRO_ENVIRONMENT_SET_HW_RENDER:
         CHECK(pixels==1);++hardware;
         CHECK(((struct retro_hw_render_callback *)data)->context_type==(api?RETRO_HW_CONTEXT_D3D12:RETRO_HW_CONTEXT_VULKAN));
         return mode>=2;
      case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT:
         request=(struct retro_hw_render_context_negotiation_interface *)data;
         request->interface_version=mode==4?0:2;return mode!=4;
      case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
         request=(struct retro_hw_render_context_negotiation_interface *)data;
         CHECK(hardware==1);++negotiations;
         if(request->interface_version==2) {++modern;return mode!=3;}
         CHECK(request->interface_version==1);++legacy;return true;
      default: return false;
   }
}
static void reset_counts(unsigned next)
{ mode=next;pixels=hardware=negotiations=modern=legacy=0; }
int main(int argc,char **argv)
{
   struct retro_game_info game;
   unsigned i;
   CHECK(argc>=2);directory=argv[1];
   if(argc>2 && !strcmp(argv[2],"providers")) {
      CHECK(xe_load_probe_providers());puts("PASS: provider failures return without terminating host");return 0;
   }
   retro_set_environment(environment);retro_init();memset(&game,0,sizeof(game));
   game.path="content://load-failure/no-game.xex";
   CHECK(disc.get_num_images);
   for(i=0;i<8;++i) {
      CHECK(xe_load_probe_seed_patches());
      reset_counts(0);CHECK(!retro_load_game(&game));
      CHECK(pixels==1 && !hardware && !negotiations && xe_load_probe_clean());
      CHECK(disc.get_num_images()==0);
      reset_counts(1);CHECK(!retro_load_game(&game));
      CHECK(pixels==1 && hardware==1 && !negotiations && xe_load_probe_clean());
      CHECK(disc.get_num_images()==0);
   }
   for(i=2;i<=4;++i) {
      reset_counts(i);CHECK(retro_load_game(&game));CHECK(xe_load_probe_pending());
      CHECK(disc.get_num_images()==1);
      if(i==2) CHECK(modern==1 && !legacy && negotiations==1);
      if(i==3) CHECK(modern==1 && legacy==1 && negotiations==2);
      if(i==4) CHECK(!modern && legacy==1 && negotiations==1);
      /* Retry a failed load without unload: retire the pending registry first. */
      reset_counts(1);CHECK(!retro_load_game(&game));CHECK(xe_load_probe_clean());
      CHECK(disc.get_num_images()==0);
   }
   if(xe_load_probe_d3d12()) {
      api=1;reset_counts(1);CHECK(!retro_load_game(&game));
      CHECK(hardware==1 && xe_load_probe_clean() && disc.get_num_images()==0);
      puts("PASS: actual D3D12 hardware rejection cleanup");
   } else puts("SKIP: D3D12 hardware-request path unavailable on this host");
   retro_unload_game();CHECK(xe_load_probe_clean());retro_deinit();CHECK(xe_load_probe_clean());
   puts("PASS: failed-load ownership, retries and Vulkan negotiation fallbacks");return 0;
}

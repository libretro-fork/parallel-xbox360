/* Independent full-EDRAM guest pixel expectations. MSVC C89; BSD license. */
#include "dxil_render_color_reference.h"
#include <stdio.h>
#include <stdlib.h>
static int write_file(const char *path, const char *suffix, const void *data, size_t bytes) {
  char *name = (char *)malloc(strlen(path)+strlen(suffix)+1); FILE *file; int ok;
  if (!name) return 0;
  strcpy(name,path); strcat(name,suffix); file = fopen(name,"wb"); free(name);
  if (!file) return 0;
  ok = fwrite(data,1,bytes,file) == bytes; if (fclose(file)) ok = 0; return ok;
}
static float factor(unsigned f, unsigned c, const float s[4], const float d[4], const float k[4]) {
  switch (f) { case 0: return 0; case 4: return s[c]; case 5: return 1-s[c]; case 6: return s[3]; case 7: return 1-s[3];
  case 8: return d[c]; case 9: return 1-d[c]; case 10: return d[3]; case 11: return 1-d[3]; case 12: return k[c];
  case 13: return 1-k[c]; case 14: return k[3]; case 15: return 1-k[3]; case 16: return s[3] < 1-d[3] ? s[3] : 1-d[3]; default: return 1; }
}
static float clamp_term(unsigned f, unsigned c, float v) { return f < 3 || f == 4 || f == 5 || f == 10 || ((f == 3 || f == 12) && c == 3) ? color_clamp(v,f == 4 || f == 5 ? -32.0f : 0.0f,f == 4 || f == 5 ? 32.0f : 1.0f) : v; }
static void blended(unsigned f, uint32_t controls, const float source[4], const uint32_t old[2], float output[4]) {
  static const float k[4] = {.2f,.4f,.6f,.8f};
  float s[4], d[4], constant[4], a, b; unsigned i, shift, sf, df, op;
  color_unpack(f,old,d);
  for (i = 0; i < 4; ++i) { s[i] = clamp_term(f,i,source[i]); constant[i] = clamp_term(f,i,k[i]); }
  for (i = 0; i < 4; ++i) {
    shift = i == 3 ? 16 : 0; sf = (controls>>shift)&31; df = (controls>>(shift+8))&31; op = (controls>>(shift+5))&7;
    a = sf ? s[i]*factor(sf,i,s,d,constant) : 0; b = df ? d[i]*factor(df,i,s,d,constant) : 0;
    a = clamp_term(f,i,a); b = clamp_term(f,i,b);
    output[i] = clamp_term(f,i,op == 1 ? a-b : op == 4 ? b-a : op == 2 ? (a < b ? a : b) : op == 3 ? (a > b ? a : b) : a+b);
  }
}
int main(int argc, char **argv) {
  static const unsigned formats[12] = {0,1,2,3,4,5,6,7,10,12,14,15};
  static const unsigned factors[15] = {0,1,4,5,6,7,8,9,10,11,12,13,14,15,16};
  float source[5][4] = {{.125f,.25f,.5f,.75f},{1,2,3,4},{-1,.0625f,.375f,1},{.875f,.5f,.25f,.125f},{.375f,6,7,8}};
  static const float sample_x[4] = {-.125f,.375f,-.375f,.125f};
  float slope;
  float destination[4] = {-24,.25f,70000,.5f}, output[4], biased[4], depth;
  uint32_t constants[128], counters[64], *golden, *initial, old[2], packed[2];
  unsigned query, variant, mode, config, scenario, msaa, sx, sy, width, height, words, bytes, tile_words, pitch;
  unsigned x, y, sample, rt, j, f, u, v, address, base, covered = 0, passed, failed_depth, failed_stencil;
  int ok;
  if (argc != 5) return 2;
  mode = (unsigned)strtoul(argv[2],NULL,10); config = (unsigned)strtoul(argv[3],NULL,10); scenario = (unsigned)strtoul(argv[4],NULL,10);
  msaa = config&3; if (msaa > 2 || (mode != 0 && mode != 14 && mode != 168 && mode != 182 && mode != 476 && mode != 504 && mode != 532) || scenario > 1144) return 2;
  variant = mode/28; query = variant >= 17 ? variant-16 : 0; mode %= 28;
  sx = config&4 ? 2 : 1; sy = config&4 ? 3 : 1; width = (scenario >= 1143 ? 64u : 80u)*sx; height = (scenario >= 1143 ? 8u : 16u)*sy;
  tile_words = 1280*sx*sy; words = (scenario == 3 ? 2048u : 48u)*tile_words; bytes = words*4; pitch = 2*tile_words;
  golden = (uint32_t *)malloc(bytes); initial = (uint32_t *)malloc(bytes); if (!golden || !initial) { free(golden); free(initial); return 1; }
  for (x = 0; x < words; ++x) initial[x] = 0x80000011;
  memset(constants,0,sizeof(constants)); memset(counters,0,sizeof(counters));
  constants[0] = (7u<<11) | (msaa<<8); constants[1] = color_bits(.5f); constants[2] = scenario == 11 || scenario == 15 ? 0x55555555 : 0; constants[3] = scenario == 12 ? UINT32_MAX : 3;
  constants[32] = pitch; constants[34] = constants[35] = 0xffff5a; constants[36] = constants[37] = 7u | (2u<<6);
  for (j = 0; j < 4; ++j) { constants[4+j] = color_bits(scenario == 10 ? (float)(1u<<j) : 1); constants[8+j] = color_bits(.2f*(float)(j+1)); }
  if ((scenario >= 4 && scenario <= 7) || (scenario >= 13 && scenario <= 15) || scenario >= 1141) {
    constants[0] |= (1u<<17) | ((scenario == 5 || scenario == 13 || scenario == 15 ? 4u : 1u)<<18) | (1u<<21);
    if (scenario == 6 || scenario == 14) { constants[0] |= 1u<<22; constants[36] = constants[37] = 3u<<3; }
    if (scenario == 7) { constants[0] |= 1u<<22; constants[0] &= ~(7u<<18); constants[0] |= 7u<<18; }
  }
  if (query == 2 && scenario == 10) constants[7] = color_bits(4);
  constants[100] = scenario; constants[101] = query; constants[102] = formats[(((config>>3)&15)%12+3)%12];
  if (scenario >= 1141) { constants[0] &= ~(7u<<18); constants[0] |= 7u<<18; }
  if (scenario == 1141) constants[0] |= 1u<<10;
  if (scenario == 1142) { constants[14] = color_bits(.125f); constants[15] = color_bits(.25f); }
  if (scenario >= 1143) { constants[12] = color_bits(4); constants[13] = color_bits(8); }
  if (scenario == 8 || scenario == 13) source[4][0] = source[0][0];
  if (scenario == 9 || scenario == 14) { constants[0] &= ~(7u<<11); constants[0] |= 1u<<11; }
  if (scenario == 11 || scenario == 15) constants[0] |= 1u<<16;
  for (rt = 0; rt < 4; ++rt) {
    f = formats[(((config>>3)&15)%12+rt)%12]; base = scenario == 3 && !rt ? 2047*tile_words : (rt+1)*8*tile_words; constants[40+rt] = base; constants[44+rt] = 0x00010001;
    if (scenario >= 16 && scenario <= 1140) { unsigned test = scenario-16, sf = factors[(test/5)%15], df = factors[(test/75)%15], op = test%5;
      constants[44+rt] = sf | (op<<5) | (df<<8) | (df<<16) | (op<<21) | (sf<<24); }
    constants[48+rt*2] = scenario == 2 ? UINT32_MAX : scenario == 1 || (config&1024) ? 0x00ff00ff : 0;
    constants[49+rt*2] = scenario == 2 ? UINT32_MAX : scenario == 1 || (config&1024) ? 0xff00ff00 : 0;
    constants[56+rt*4] = constants[57+rt*4] = color_bits(f == 4 || f == 5 ? -32.0f : 0.0f);
    constants[58+rt*4] = constants[59+rt*4] = color_bits(f == 4 || f == 5 ? 32.0f : 1.0f);
    color_pack(f,destination,packed);
    for (x = base; x < base+8*tile_words; x += color_words(f)) for (j = 0; j < color_words(f); ++j) initial[(x+j)%words] = packed[j];
  }
  memcpy(golden,initial,bytes); failed_depth = scenario == 5 || scenario == 13 || scenario == 15; failed_stencil = scenario == 6 || scenario == 14; passed = !failed_depth && !failed_stencil;
  for (y = 0; y < height; ++y) for (x = 0; x < width; ++x) for (sample = 0; sample < (1u<<msaa); ++sample) {
    if (scenario == 8 || scenario == 13 || ((scenario == 9 || scenario == 14) && variant != 6)) continue;
    if (variant != 6 && (scenario == 11 || scenario == 15) && sample >= (msaa ? (msaa == 1 ? 1u : 3u) : 1u)) continue;
    ++covered; passed = !failed_depth && !failed_stencil;
    u = msaa == 0 ? x/sx : msaa == 1 ? (x/sx/4)*4+(x/sx)%2+2*sample : (x/sx/2)*4+(x/sx)%2+2*(sample%2);
    v = msaa == 0 ? y/sy : (y/sy/2)*4+(y/sy)%2+2*(msaa == 1 ? (x/sx/2)%2 : sample/2);
    u = u*sx+x%sx; v = v*sy+y%sy;
    base = (v/(16*sy))*pitch+(u/(80*sx))*tile_words+(v%(16*sy))*(80*sx);
    address = base+(u%(80*sx)+40*sx)%(80*sx);
    if ((scenario >= 4 && scenario <= 7) || (scenario >= 13 && scenario <= 15) || scenario >= 1141) {
      depth = mode >= 14 && !query ? .375f : .25f;
      if ((mode < 14 || query) && scenario == 1142) depth += .125f;
      if ((mode < 14 || query) && scenario >= 1143) {
        slope = .125f/(float)width;
        depth = .125f+slope*((float)x+.5f); depth += slope*(scenario == 1144 ? 8.0f : 4.0f);
        if (msaa) depth += slope*sample_x[msaa == 1 && sample == 1 ? 3 : sample];
      }
      if (passed && scenario == 1141) golden[address] = (((color_bits(depth)-0x38000000+3+((color_bits(depth)>>3)&1))>>3)<<8) | 0x11;
      else if (passed) golden[address] = (color_even((double)(depth*16777215.0f))<<8) | (scenario == 7 ? 0x5a : 0x11);
      if (failed_stencil) golden[address] = 0x80000012;
    }
    if (!passed) continue;
    for (rt = 0; rt < 4; ++rt) {
      if (variant == 6 && !rt) continue;
      f = formats[(((config>>3)&15)%12+rt)%12];
      address = constants[40+rt]+(color_words(f) == 1 ? base+u%(80*sx) : (v/(16*sy))*pitch*2+(u/(40*sx))*tile_words+(v%(16*sy))*(80*sx)+2*(u%(40*sx)));
      address %= words;
      for (j = 0; j < 4; ++j) output[j] = source[rt][j]*(scenario == 10 ? (float)(1u<<rt) : 1);
      if (rt == 3 && query) {
        if (query == 2) { output[0] = 1.0f/(float)width; output[1] = output[2] = 0; output[3] = 1.0f/(float)height; }
        else { output[0] = ((float)x+.5f)/(float)width; output[1] = ((float)y+.5f)/(float)height;
          for (j = 0; j < 2; ++j) { if (query == 1) output[j] *= 4; output[j] += -.5f+1.5f/1024; output[j] -= (float)floor((double)output[j]); }
          output[2] = output[3] = 0; }
        if (scenario == 10) for (j = 0; j < 4; ++j) output[j] *= query == 2 ? 4 : 8;
      }
      old[0] = golden[address]; old[1] = golden[address+1];
      if (scenario >= 16 && scenario <= 1140 && !(config&128)) { memcpy(biased,output,sizeof(biased)); blended(f,constants[44+rt],biased,old,output); }
      color_pack(f,output,packed);
      for (j = 0; j < color_words(f); ++j) golden[address+j] = (old[j]&constants[48+rt*2+j]) | (packed[j]&~constants[48+rt*2+j]);
    }
  }
  if (scenario != 12) { counters[14] = passed ? covered : 0; if (!(config&256)) { counters[13] = failed_depth ? (mode < 14 || query ? width*height*(1u<<msaa) : covered) : 0; counters[15] = failed_stencil ? (mode < 14 || query ? width*height*(1u<<msaa) : covered) : 0; } }
  ok = write_file(argv[1],".initial",initial,bytes) && write_file(argv[1],".expected",golden,bytes) && write_file(argv[1],".constants",constants,sizeof(constants)) && write_file(argv[1],".colors",source,sizeof(source)) && write_file(argv[1],".counters",counters,sizeof(counters));
  free(initial); free(golden); return !ok;
}

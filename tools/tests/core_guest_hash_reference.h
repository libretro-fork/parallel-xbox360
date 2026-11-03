#include <stdint.h>
#include <stddef.h>
#include <string.h>
static uint32_t ref_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void ref_put32(uint8_t *p, uint32_t v) {
  p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static uint64_t ref_be64(const uint8_t *p) {
  return ((uint64_t)ref_be32(p)<<32) | ref_be32(p+4);
}
static void ref_put64(uint8_t *p, uint64_t v) {
  ref_put32(p,(uint32_t)(v>>32)); ref_put32(p+4,(uint32_t)v);
}

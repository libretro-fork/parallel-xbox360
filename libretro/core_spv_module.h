/* Exclusive module index of borrowed encoded packets. Index reset invalidates
 * all IDs without clearing/copying retained storage. Records must place their
 * packet first; the typed wrapper verifies pointer interconvertibility. */
#ifndef XE_CORE_SPV_MODULE_H
#define XE_CORE_SPV_MODULE_H
#include "core_spv_index.h"
#include "core_spv_packet.h"
enum { XE_SPV_MODULE_TYPE_POINTER = 32 };
static INLINE int xe_spv_module_map(xe_spv_index *index, xe_spv_packet *packet) {
   return xe_spv_index_set(index, xe_spv_packet_result(packet), packet);
}
static INLINE xe_spv_packet *xe_spv_module_get(const xe_spv_index *index, uint32_t id) {
   return (xe_spv_packet *)xe_spv_index_get(index, id);
}
static INLINE uint32_t xe_spv_module_type(const xe_spv_index *index, uint32_t id) {
   const xe_spv_packet *packet = xe_spv_module_get(index, id);
   return packet ? xe_spv_packet_type(packet) : 0;
}
/* Malformed or missing pointer records leave the caller's value unchanged. */
static INLINE int xe_spv_module_storage_class(const xe_spv_index *index,
      uint32_t id, uint32_t *storage) {
   const xe_spv_packet *packet = xe_spv_module_get(index, id);
   if (!packet || xe_spv_packet_opcode(packet) != XE_SPV_MODULE_TYPE_POINTER ||
       !packet->count || xe_spv_packet_is_id(packet, 0)) return 0;
   *storage = xe_spv_packet_const_operands(packet)[0];
   return 1;
}
#endif

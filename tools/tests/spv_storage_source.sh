# Pick storage from the same complete source revision as its IR.
spv_storage_source() {
 if test -f "$1/libretro/core_spv_packet.c"; then
  printf '%s\n' "$1/libretro/core_spv_packet.c"
 else
  printf '%s\n' "$1/libretro/core_spv_operands.c"
 fi
}
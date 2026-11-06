# Pick storage from the same complete source revision as its IR.
spv_storage_source() {
 if test -f "$1/libretro/core_spv_packet.c"; then
  printf '%s\n' "$1/libretro/core_spv_packet.c"
 else
  printf '%s\n' "$1/libretro/core_spv_operands.c"
 fi
}
# Historical complete revisions may still need their standalone postprocessor
# when compiling their own older fixtures. Current core revisions omit it.
# Keep every path a separate argument, including workspaces containing spaces.
spv_compile_revision() {
 xe_spv_revision_root=$1
 shift
 if test -f "$xe_spv_revision_root/src/xenia/gpu/spirv/SpvPostProcess.cpp"; then
  "$@" "$xe_spv_revision_root/src/xenia/gpu/spirv/SpvPostProcess.cpp"
 else
  "$@"
 fi
}

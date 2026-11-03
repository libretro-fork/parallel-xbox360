/* Interoperability oracle for the previous guest SHA-256 implementation. */
#include <cstring>
#include "guest_sha256_reference.cpp"
extern "C" void xe_legacy_sha256_update(uint32_t* words, uint8_t* buffer,
    uint32_t* count, const uint8_t* input, size_t size) {
  sha256::SHA256 state;
  std::memcpy(state.getHashValues(), words, 32);
  std::memcpy(state.getBuffer(), buffer, 64);
  state.setTotalSize(*count);
  state.add(input, size);
  std::memcpy(words, state.getHashValues(), 32);
  std::memcpy(buffer, state.getBuffer(), 64);
  *count = static_cast<uint32_t>(state.getTotalSize());
}
extern "C" void xe_legacy_sha256_final(const uint32_t* words,
    const uint8_t* buffer, uint32_t count, uint8_t* digest) {
  sha256::SHA256 state;
  std::memcpy(state.getHashValues(), words, 32);
  std::memcpy(state.getBuffer(), buffer, 64);
  state.setTotalSize(count);
  state.getHash(digest);
}

/* Reference-only boundary: the former UTF-8 -> native filesystem join. */
#include <filesystem>
#include <string>
#include <cstring>
extern "C" int xe_disc_path_reference(const char* reference, const char* input,
                                       char* output, size_t capacity) {
  try {
    std::filesystem::path path = std::filesystem::u8path(input);
    if (path.is_relative()) path = std::filesystem::u8path(reference).parent_path() / path;
    const auto bytes = path.u8string();
    if (bytes.size() >= capacity) return 0;
    std::memcpy(output, bytes.data(), bytes.size());
    output[bytes.size()] = 0;
    return 1;
  } catch (...) { return 0; }
}

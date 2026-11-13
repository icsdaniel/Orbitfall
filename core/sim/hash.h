#pragma once
// 64-bit FNV-1a used for the world state hash. Values are fed field by field
// (never whole structs) so padding bytes can not leak into the hash.
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace orbitfall {

struct Hasher {
  uint64_t state = 0xCBF29CE484222325ull;

  void bytes(const void* data, size_t size) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint64_t h = state;
    for (size_t i = 0; i < size; ++i) {
      h ^= p[i];
      h *= 0x100000001B3ull;
    }
    state = h;
  }

  // One arithmetic or enum value.
  template <typename T>
  void add(T value) {
    static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value, "hash scalars only");
    bytes(&value, sizeof(T));
  }

  // A contiguous array of arithmetic values (a pool field).
  template <typename T>
  void array(const T* data, size_t count) {
    static_assert(std::is_arithmetic<T>::value, "hash scalar arrays only");
    if (count) bytes(data, count * sizeof(T));
  }
};

}  // namespace orbitfall

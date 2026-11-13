#pragma once
// Fixed-capacity structure-of-arrays pools (CLAUDE.md hard rule 1).
//
// Declare a pool from a field list; every field becomes a named array:
//
//   #define ENEMY_FIELDS(F) F(float, posX) F(float, posY) F(float, hp) F(uint16_t, type)
//   ORBITFALL_DECLARE_POOL(EnemyPool, ENEMY_FIELDS)
//
//   EnemyPool enemies;
//   enemies.reserve(4096);                 // the only allocation, done at load
//   uint32_t i = enemies.add();            // new entity, fields zeroed; kInvalidIndex when full
//   if (i != kInvalidIndex) enemies.hp[i] = 3.0f;
//   for (uint32_t i = 0; i < enemies.count; ++i) enemies.posX[i] += enemies.velX[i] * dt;
//   enemies.remove(i);                     // swap-remove: the last entity moves into slot i
//
// Indices are not stable across remove(). To remove while iterating, either
// walk backwards or do not advance `i` after a remove:
//
//   for (uint32_t i = 0; i < pool.count;) { if (dead(i)) pool.remove(i); else ++i; }
//
// Fields must be arithmetic types. All arrays live in one 64-byte aligned block,
// each field starting on its own cache line. Pools are not copyable.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "sim/hash.h"

namespace orbitfall {

constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

namespace pool_detail {
constexpr size_t kAlign = 64;
inline size_t alignUp(size_t n) { return (n + (kAlign - 1)) & ~(kAlign - 1); }
// Returns nullptr on failure (never throws). Defined in pool.cpp.
void* allocate(size_t bytes);
void release(void* block);
}  // namespace pool_detail

}  // namespace orbitfall

#define ORBITFALL_POOL_DECL_(T, name)                                         \
  static_assert(std::is_arithmetic<T>::value, "pool fields must be scalars"); \
  T* name = nullptr;
#define ORBITFALL_POOL_SIZE_(T, name) \
  bytes_ = ::orbitfall::pool_detail::alignUp(bytes_) + sizeof(T) * static_cast<size_t>(cap);
#define ORBITFALL_POOL_BIND_(T, name)                    \
  offset_ = ::orbitfall::pool_detail::alignUp(offset_);  \
  name = reinterpret_cast<T*>(base_ + offset_);          \
  offset_ += sizeof(T) * static_cast<size_t>(cap);
#define ORBITFALL_POOL_NULL_(T, name) name = nullptr;
#define ORBITFALL_POOL_ZERO_(T, name) name[index_] = T();
#define ORBITFALL_POOL_MOVE_(T, name) name[index] = name[last_];
#define ORBITFALL_POOL_HASH_(T, name) hasher.array(name, count);
#define ORBITFALL_POOL_COUNT_(T, name) +1

#define ORBITFALL_DECLARE_POOL(PoolName, FIELDS)                                         \
  struct PoolName {                                                                      \
    uint32_t count = 0;         /* live entities: valid indices are [0, count) */        \
    uint32_t capacity = 0;      /* fixed by reserve() */                                 \
    uint32_t overflowCount = 0; /* add() calls refused because the pool was full */      \
    FIELDS(ORBITFALL_POOL_DECL_)                                                         \
    static constexpr uint32_t kFieldCount = 0 FIELDS(ORBITFALL_POOL_COUNT_);             \
                                                                                         \
    PoolName() = default;                                                                \
    ~PoolName() { ::orbitfall::pool_detail::release(block_); }                           \
    PoolName(const PoolName&) = delete;                                                  \
    PoolName& operator=(const PoolName&) = delete;                                       \
                                                                                         \
    /* Allocates storage for `cap` entities and empties the pool. Call at load.       */ \
    /* Returns false (capacity 0) if the allocation fails.                            */ \
    bool reserve(uint32_t cap) {                                                         \
      ::orbitfall::pool_detail::release(block_);                                         \
      block_ = nullptr;                                                                  \
      count = 0;                                                                         \
      capacity = 0;                                                                      \
      overflowCount = 0;                                                                 \
      FIELDS(ORBITFALL_POOL_NULL_)                                                       \
      if (cap == 0) return true;                                                         \
      size_t bytes_ = 0;                                                                 \
      FIELDS(ORBITFALL_POOL_SIZE_)                                                       \
      block_ = ::orbitfall::pool_detail::allocate(bytes_);                               \
      if (block_ == nullptr) return false;                                               \
      unsigned char* base_ = static_cast<unsigned char*>(block_);                        \
      size_t offset_ = 0;                                                                \
      FIELDS(ORBITFALL_POOL_BIND_)                                                       \
      capacity = cap;                                                                    \
      return true;                                                                       \
    }                                                                                    \
                                                                                         \
    /* Adds one entity with every field zeroed. Returns its index, or kInvalidIndex   */ \
    /* (and counts the overflow) when the pool is full.                               */ \
    uint32_t add() {                                                                     \
      if (count >= capacity) {                                                           \
        ++overflowCount;                                                                 \
        return ::orbitfall::kInvalidIndex;                                               \
      }                                                                                  \
      const uint32_t index_ = count++;                                                   \
      FIELDS(ORBITFALL_POOL_ZERO_)                                                       \
      return index_;                                                                     \
    }                                                                                    \
                                                                                         \
    /* Swap-remove: the last entity (old index count-1) takes slot `index`.           */ \
    /* Returns false and does nothing when `index` is out of range.                   */ \
    bool remove(uint32_t index) {                                                        \
      if (index >= count) return false;                                                  \
      const uint32_t last_ = --count;                                                    \
      if (index != last_) {                                                              \
        FIELDS(ORBITFALL_POOL_MOVE_)                                                     \
      }                                                                                  \
      return true;                                                                       \
    }                                                                                    \
                                                                                         \
    /* Removes every entity; keeps the storage. */                                       \
    void clear() { count = 0; }                                                          \
    bool full() const { return count >= capacity; }                                      \
                                                                                         \
    /* Feeds count and the live part of every field into the state hash. */              \
    void hashInto(::orbitfall::Hasher& hasher) const {                                   \
      hasher.add(count);                                                                 \
      FIELDS(ORBITFALL_POOL_HASH_)                                                       \
    }                                                                                    \
                                                                                         \
   private:                                                                              \
    void* block_ = nullptr;                                                              \
  };

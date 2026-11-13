// Replaces the global allocation functions for core_tests so tests can prove
// that code paths do not allocate. Every form of operator new bumps one counter.
#include "alloc_counter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<uint64_t> g_allocations{0};

void* plainAlloc(std::size_t size) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size ? size : 1);
}

void* alignedAlloc(std::size_t size, std::size_t align) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
#if defined(_MSC_VER)
  return _aligned_malloc(size ? size : 1, align);
#else
  if (align < sizeof(void*)) align = sizeof(void*);
  void* p = nullptr;
  return posix_memalign(&p, align, size ? size : 1) == 0 ? p : nullptr;
#endif
}

void alignedFree(void* p) {
#if defined(_MSC_VER)
  _aligned_free(p);
#else
  std::free(p);
#endif
}

}  // namespace

namespace orbitfall_test {
uint64_t allocationCount() { return g_allocations.load(std::memory_order_relaxed); }
}  // namespace orbitfall_test

void* operator new(std::size_t size) {
  void* p = plainAlloc(size);
  if (!p) std::abort();
  return p;
}
void* operator new[](std::size_t size) {
  void* p = plainAlloc(size);
  if (!p) std::abort();
  return p;
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return plainAlloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return plainAlloc(size); }
void* operator new(std::size_t size, std::align_val_t align) {
  void* p = alignedAlloc(size, static_cast<std::size_t>(align));
  if (!p) std::abort();
  return p;
}
void* operator new[](std::size_t size, std::align_val_t align) {
  void* p = alignedAlloc(size, static_cast<std::size_t>(align));
  if (!p) std::abort();
  return p;
}
void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
  return alignedAlloc(size, static_cast<std::size_t>(align));
}
void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
  return alignedAlloc(size, static_cast<std::size_t>(align));
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { alignedFree(p); }

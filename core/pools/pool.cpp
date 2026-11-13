#include "pools/pool.h"

#include <new>

namespace orbitfall {
namespace pool_detail {

void* allocate(size_t bytes) {
  return ::operator new(alignUp(bytes), std::align_val_t(kAlign), std::nothrow);
}

void release(void* block) {
  if (block) ::operator delete(block, std::align_val_t(kAlign));
}

}  // namespace pool_detail
}  // namespace orbitfall

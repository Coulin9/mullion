#include "util_win32_compat.h"

#include <mutex>
#include <new>
#include <sys/mman.h>
#include <unordered_map>

namespace {
std::mutex mapping_mutex;
std::unordered_map<void *, size_t> mappings;
}

void *
VirtualAlloc(void *address, size_t length, DWORD flags, DWORD protection) {
  // The D3D12 recorder and CPU-visible buffers request fresh committed storage.
  if (address || !length || (flags & ~MEM_WRITE_WATCH) != (MEM_RESERVE | MEM_COMMIT) ||
      protection != PAGE_READWRITE)
    return nullptr;
  void *mapping = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (mapping == MAP_FAILED)
    return nullptr;
  try {
    std::lock_guard lock(mapping_mutex);
    mappings.emplace(mapping, length);
  } catch (const std::bad_alloc &) {
    munmap(mapping, length);
    return nullptr;
  }
  return mapping;
}

BOOL
VirtualFree(void *address, size_t length, DWORD flags) {
  if (!address || length || flags != MEM_RELEASE)
    return FALSE;
  std::lock_guard lock(mapping_mutex);
  auto mapping = mappings.find(address);
  if (mapping == mappings.end() || munmap(address, mapping->second))
    return FALSE;
  mappings.erase(mapping);
  return TRUE;
}

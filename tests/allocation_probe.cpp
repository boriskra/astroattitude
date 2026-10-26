// Test executables only: allocation accounting and one-shot failures.
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <stdexcept>
std::atomic<size_t> aa_live_bytes{0}, aa_peak_bytes{0};
thread_local long aa_fail_after = -1;
thread_local bool aa_unexpected = false;
namespace {
struct Header {
  void *base;
  size_t size;
};
void *allocate(size_t n, size_t alignment) {
  if (aa_unexpected) {
    aa_unexpected = false;
    throw std::runtime_error("Injected unexpected internal exception");
  }
  if (aa_fail_after == 0) {
    aa_fail_after = -1;
    throw std::bad_alloc();
  }
  if (aa_fail_after > 0)
    --aa_fail_after;
  if (n > size_t(-1) - alignment - sizeof(Header))
    throw std::bad_alloc();
  void *base = std::malloc(n + alignment + sizeof(Header));
  if (!base)
    throw std::bad_alloc();
  auto raw = reinterpret_cast<size_t>(base) + sizeof(Header);
  auto address = (raw + alignment - 1) & ~(alignment - 1);
  auto *h = reinterpret_cast<Header *>(address) - 1;
  h->base = base;
  h->size = n;
  size_t live = aa_live_bytes.fetch_add(n) + n, peak = aa_peak_bytes.load();
  while (live > peak && !aa_peak_bytes.compare_exchange_weak(peak, live)) {
  }
  return reinterpret_cast<void *>(address);
}
void release(void *p) {
  if (!p)
    return;
  auto *h = reinterpret_cast<Header *>(p) - 1;
  aa_live_bytes.fetch_sub(h->size);
  std::free(h->base);
}
} // namespace
void *operator new(size_t n) { return allocate(n, alignof(std::max_align_t)); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { release(p); }
void operator delete[](void *p) noexcept { release(p); }
void operator delete(void *p, size_t) noexcept { release(p); }
void operator delete[](void *p, size_t) noexcept { release(p); }
void *operator new(size_t n, std::align_val_t a) {
  return allocate(n, size_t(a));
}
void *operator new[](size_t n, std::align_val_t a) {
  return allocate(n, size_t(a));
}
void operator delete(void *p, std::align_val_t) noexcept { release(p); }
void operator delete[](void *p, std::align_val_t) noexcept { release(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { release(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept {
  release(p);
}

#pragma once
// Test-only C++ allocation instrumentation. Include in one TU per executable.
#include <cstdlib>
#include <cstdio>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace realtimecheck {
thread_local bool rendering=false;
thread_local unsigned heapCalls=0;
inline void Count() { if (rendering) ++heapCalls; }
struct Scope
{
  Scope() { if (rendering) std::abort(); heapCalls=0; rendering=true; }
  ~Scope()
  {
    rendering=false;
    if (heapCalls) {std::fprintf(stderr,"C++ heap call during guarded render\n");std::abort();}
  }
};
}
void* operator new(std::size_t size)
{realtimecheck::Count();if (void* p=std::malloc(size ? size : 1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {realtimecheck::Count();std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#if defined(__cpp_aligned_new)
void* operator new(std::size_t size,std::align_val_t alignment)
{
  realtimecheck::Count();void* p=nullptr;
#if defined(_WIN32)
  p=_aligned_malloc(size ? size : 1,static_cast<size_t>(alignment));
#else
  if (posix_memalign(&p,static_cast<size_t>(alignment),size ? size : 1)) p=nullptr;
#endif
  if (p) return p;throw std::bad_alloc();
}
void* operator new[](std::size_t size,std::align_val_t alignment) {return ::operator new(size,alignment);}
void operator delete(void* p,std::align_val_t) noexcept
{
  realtimecheck::Count();
#if defined(_WIN32)
  _aligned_free(p);
#else
  std::free(p);
#endif
}
void operator delete[](void* p,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
#endif

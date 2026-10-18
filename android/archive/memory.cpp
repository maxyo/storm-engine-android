// Bound allocations made by statically linked C archive/codec libraries.
// C allocators keep their NULL-returning contract. Only libarchive's explicit
// fatal hook throws, through C code built with -fexceptions, into a C++ boundary.
#include "memory.hpp"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cerrno>
#include <new>
#include <exception>
namespace {
constexpr size_t cap=768ULL*1024*1024;
std::atomic<size_t> used{0};
thread_local bool failed=false;
thread_local storm_archive::CodecArena *current_arena=nullptr;
struct alignas(std::max_align_t) Header {
 size_t size;
 Header *previous,*next;
 storm_archive::CodecArena *owner;
};
#ifdef STORM_ARCHIVE_TESTING
thread_local size_t fail_at=0,attempts=0,test_quota=cap;
thread_local bool persistent=false;
#endif
bool reserve(size_t n) noexcept {
#ifdef STORM_ARCHIVE_TESTING
 ++attempts;
 if(fail_at&&(attempts==fail_at||(persistent&&attempts>=fail_at))) {
  storm_archive::mark_allocation_failure();return false;
 }
 const size_t limit=test_quota;
#else
 const size_t limit=cap;
#endif
 size_t old=used.load(std::memory_order_relaxed);
 do {if(old>limit||n>limit-old){storm_archive::mark_allocation_failure();return false;}}
 while(!used.compare_exchange_weak(old,old+n,std::memory_order_relaxed));
 return true;
}
struct ArchiveFatal final:std::exception {
 const char *what()const noexcept override{return "Internal archive reader failure";}
};
}
extern "C" {
void *__real_malloc(size_t);
void __real_free(void*);
void __wrap_free(void*);
void *__wrap_malloc(size_t);
}
namespace storm_archive {
bool allocation_failed(){return failed;}
void clear_allocation_failure(){failed=false;}
void mark_allocation_failure() noexcept {
 errno=ENOMEM;failed=true;if(current_arena)current_arena->poisoned=true;
}
void *allocate_result(std::size_t size) noexcept {
 CodecArena *saved=current_arena;current_arena=nullptr;
 void *result=__wrap_malloc(size);current_arena=saved;
 return result;
}
CodecArena::CodecArena() noexcept:previous(current_arena){current_arena=this;}
CodecArena::~CodecArena() noexcept {
 // Normal cleanup should have released all nodes. On partial initialization,
 // OOM, or the fatal-hook exception, free unreachable C allocations directly.
 // Do not re-enter a partially initialized/poisoned libarchive object.
 while(head)__wrap_free(static_cast<Header*>(head)+1);
 current_arena=previous;
}
}
extern "C" {
void *__wrap_malloc(size_t n) {
 if(n>std::numeric_limits<size_t>::max()-sizeof(Header)) {storm_archive::mark_allocation_failure();return nullptr;}
 if(!reserve(n))return nullptr;
 auto *h=static_cast<Header*>(__real_malloc(n+sizeof(Header)));
 if(!h){storm_archive::mark_allocation_failure();used.fetch_sub(n,std::memory_order_relaxed);return nullptr;}
 h->size=n;h->owner=current_arena;h->previous=nullptr;h->next=nullptr;
 if(current_arena){h->next=static_cast<Header*>(current_arena->head);if(h->next)h->next->previous=h;current_arena->head=h;}
 return h+1;
}
void __wrap_free(void *p) {
 if(!p)return;auto *h=static_cast<Header*>(p)-1;
 if(h->owner){
  if(h->previous)h->previous->next=h->next;else h->owner->head=h->next;
  if(h->next)h->next->previous=h->previous;
 }
 used.fetch_sub(h->size,std::memory_order_relaxed);__real_free(h);
}
void *__wrap_calloc(size_t count,size_t size) {
 if(size&&count>std::numeric_limits<size_t>::max()/size){storm_archive::mark_allocation_failure();return nullptr;}
 auto *p=__wrap_malloc(count*size);if(p)memset(p,0,count*size);return p;
}
void *__wrap_realloc(void *p,size_t n) {
 if(!p)return __wrap_malloc(n);if(!n){__wrap_free(p);return nullptr;}
 auto *h=static_cast<Header*>(p)-1;
 auto *next=__wrap_malloc(n);if(!next)return nullptr;
 memcpy(next,p,h->size<n?h->size:n);__wrap_free(p);return next;
}
char *__wrap_strdup(const char *s) {
 size_t n=strlen(s)+1;auto *p=static_cast<char*>(__wrap_malloc(n));if(p)memcpy(p,s,n);return p;
}
// Called only by the patched __archive_errx, never by the normal allocators.
// No dynamic diagnostics or C++ runtime_error string construction on this path.
[[noreturn]] void storm_archive_libarchive_fatal(int,const char *message) {
 if(current_arena)current_arena->poisoned=true;
 if(failed||(message&&(strstr(message,"memory")||strstr(message,"Memory")) )) {
  storm_archive::mark_allocation_failure();throw std::bad_alloc();
 }
 throw ArchiveFatal();
}
#ifdef STORM_ARCHIVE_TESTING
void storm_archive_test_fault(size_t at,int repeat,size_t quota) {
 attempts=0;fail_at=at;persistent=repeat!=0;test_quota=quota?quota:cap;
}
size_t storm_archive_test_used(){return used.load(std::memory_order_relaxed);}
size_t storm_archive_test_attempts(){return attempts;}
#endif
}

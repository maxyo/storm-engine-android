#pragma once
#include <cstddef>
namespace storm_archive {
// Owns every wrapped C allocation made while it is current on this thread.
// Registration is intrusive: creating or cleaning an arena never allocates.
// libarchive owns no input/output descriptors: those remain C++ RAII objects.
struct CodecArena {
 void *head=nullptr;
 CodecArena *previous=nullptr;
 bool poisoned=false;
 CodecArena() noexcept;
 ~CodecArena() noexcept;
 CodecArena(const CodecArena&)=delete;
 CodecArena& operator=(const CodecArena&)=delete;
};
void mark_allocation_failure() noexcept;
// Public manifest buffers outlive a reader, including reentrant callback calls.
void *allocate_result(std::size_t size) noexcept;
}

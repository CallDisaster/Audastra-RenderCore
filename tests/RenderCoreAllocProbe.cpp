// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

// 仅测试可执行文件替换 operator new；产品库不链接本文件。
#include "RenderCoreAllocProbe.h"
#include <cstdlib>
#include <cstddef>
#include <limits>
#include <new>

namespace {
thread_local bool measuring = false;
thread_local std::uint64_t allocations = 0;
void* Allocate(std::size_t bytes, std::size_t alignment) {
    if (measuring) ++allocations;
    if (alignment < alignof(void*)) alignment = alignof(void*);
    if (bytes == 0) bytes = 1;
    if (bytes > std::numeric_limits<std::size_t>::max() - alignment - sizeof(void*)) throw std::bad_alloc();
    auto* raw = std::malloc(bytes + alignment + sizeof(void*));
    if (!raw) throw std::bad_alloc();
    const auto start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
    const auto aligned = (start + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
    auto* result = reinterpret_cast<void*>(aligned);
    *(static_cast<void**>(result) - 1) = raw;
    return result;
}
void Release(void* pointer) noexcept {
    if (pointer) std::free(*(static_cast<void**>(pointer) - 1));
}
}
namespace RenderCoreAllocProbe {
void Begin() noexcept { allocations = 0; measuring = true; }
std::uint64_t End() noexcept { measuring = false; return allocations; }
}
void* operator new(std::size_t n) { return Allocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return Allocate(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a) { return Allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return Allocate(n, static_cast<std::size_t>(a)); }
void operator delete(void* p) noexcept { Release(p); }
void operator delete[](void* p) noexcept { Release(p); }
void operator delete(void* p, std::size_t) noexcept { Release(p); }
void operator delete[](void* p, std::size_t) noexcept { Release(p); }
void operator delete(void* p, std::align_val_t) noexcept { Release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { Release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { Release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { Release(p); }

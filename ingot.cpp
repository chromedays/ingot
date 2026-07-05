#include "ingot.h"

#include <cstdlib>

namespace ingot {

void* heap_allocator_t::alloc(int64_t bytes, int64_t align) {
    if (align <= static_cast<int64_t>(alignof(std::max_align_t))) {
        return std::malloc(static_cast<size_t>(bytes));
    }
    return std::aligned_alloc(static_cast<size_t>(align), static_cast<size_t>(bytes));
}

void heap_allocator_t::free(void* ptr, int64_t /*bytes*/) {
    std::free(ptr);
}

bool heap_allocator_t::resize(void* /*ptr*/, int64_t /*old_bytes*/,
                              int64_t /*new_bytes*/, int64_t /*align*/) {
    return false;
}

void arena_allocator_t::construct(void* backing_buffer, int64_t buffer_size) {
    buffer_ = backing_buffer;
    size_   = buffer_size;
    offset_ = 0;
}

void arena_allocator_t::destroy() {
    buffer_ = nullptr;
    size_   = 0;
    offset_ = 0;
}

void arena_allocator_t::reset() {
    offset_ = 0;
}

void* arena_allocator_t::alloc(int64_t bytes, int64_t align) {
    if (bytes <= 0 || align <= 0) {
        return nullptr;
    }

    uintptr_t raw = reinterpret_cast<uintptr_t>(buffer_) + static_cast<uintptr_t>(offset_);
    uintptr_t aligned = (raw + static_cast<uintptr_t>(align) - 1) & ~(static_cast<uintptr_t>(align) - 1);
    int64_t padding = static_cast<int64_t>(aligned - raw);
    int64_t total = padding + bytes;

    if (offset_ + total > size_) {
        return nullptr;
    }

    offset_ += total;
    return reinterpret_cast<void*>(aligned);
}

void arena_allocator_t::free(void* /*ptr*/, int64_t /*bytes*/) {
    /* no-op: arena does not reclaim individual allocations */
}

bool arena_allocator_t::resize(void* ptr, int64_t old_bytes,
                               int64_t new_bytes, int64_t /*align*/) {
    char* end  = static_cast<char*>(ptr) + old_bytes;
    char* top  = static_cast<char*>(buffer_) + offset_;
    if (end != top) {
        return false;
    }
    if (new_bytes > old_bytes) {
        int64_t growth = new_bytes - old_bytes;
        if (offset_ + growth > size_) {
            return false;
        }
    }
    offset_ += (new_bytes - old_bytes);
    return true;
}

string_t str_from_cstr(const char* s) {
    ingot_assert_(s != nullptr, "str_from_cstr: null pointer");
    return string_t{.data = s, .len = static_cast<int64_t>(std::strlen(s))};
}

bool str_equal(string_t a, string_t b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return std::memcmp(a.data, b.data, static_cast<size_t>(a.len)) == 0;
}

} // namespace ingot

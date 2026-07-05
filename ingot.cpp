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

namespace {

void sb_ensure_capacity(string_builder_t& b, int64_t needed) {
    if (needed <= b.capacity) return;
    int64_t new_cap = b.capacity > 0 ? b.capacity * 2 : 32;
    if (new_cap < needed) new_cap = needed;
    if (b.data != nullptr && b.alloc->resize(b.data, b.capacity, new_cap, 1)) {
        b.capacity = new_cap;
        return;
    }
    char* new_data = static_cast<char*>(b.alloc->alloc(new_cap, 1));
    ingot_assert_(new_data != nullptr,
                  "sb_ensure_capacity: allocation failed (needed=%lld)",
                  static_cast<long long>(needed));
    if (b.len > 0) {
        std::memcpy(new_data, b.data, static_cast<size_t>(b.len));
    }
    if (b.data != nullptr) {
        b.alloc->free(b.data, b.capacity);
    }
    b.data = new_data;
    b.capacity = new_cap;
}

} // namespace

void sb_create(string_builder_t& b, allocator_t& a, int64_t initial_capacity) {
    ingot_assert_(initial_capacity >= 0,
                  "sb_create: negative capacity (capacity=%lld)",
                  static_cast<long long>(initial_capacity));
    b.len = 0;
    b.capacity = initial_capacity;
    b.alloc = &a;
    b.data = nullptr;
    if (initial_capacity > 0) {
        b.data = static_cast<char*>(a.alloc(initial_capacity, 1));
        ingot_assert_(b.data != nullptr,
                      "sb_create: allocation failed (capacity=%lld)",
                      static_cast<long long>(initial_capacity));
    }
}

void sb_destroy(string_builder_t& b) {
    if (b.data != nullptr) {
        b.alloc->free(b.data, b.capacity);
    }
    b.data = nullptr;
    b.len = 0;
    b.capacity = 0;
    b.alloc = nullptr;
}

void sb_append_bytes(string_builder_t& b, const char* p, int64_t n) {
    ingot_assert_(n >= 0, "sb_append_bytes: negative length (n=%lld)",
                  static_cast<long long>(n));
    if (n == 0) return;
    ingot_assert_(p != nullptr, "sb_append_bytes: null pointer with n>0");
    sb_ensure_capacity(b, b.len + n);
    std::memcpy(b.data + b.len, p, static_cast<size_t>(n));
    b.len += n;
}

void sb_append_char(string_builder_t& b, char c) {
    sb_ensure_capacity(b, b.len + 1);
    b.data[b.len] = c;
    b.len++;
}

void sb_append_cstr(string_builder_t& b, const char* s) {
    ingot_assert_(s != nullptr, "sb_append_cstr: null pointer");
    sb_append_bytes(b, s, static_cast<int64_t>(std::strlen(s)));
}

void sb_append_view(string_builder_t& b, string_t v) {
    sb_append_bytes(b, v.data, v.len);
}

void sb_reserve(string_builder_t& b, int64_t total_capacity) {
    ingot_assert_(total_capacity >= 0, "sb_reserve: negative capacity");
    if (total_capacity <= b.capacity) return;
    sb_ensure_capacity(b, total_capacity);
}

char* sb_to_cstring(const string_builder_t& b, allocator_t& alloc) {
    char* out = static_cast<char*>(alloc.alloc(b.len + 1, 1));
    ingot_assert_(out != nullptr, "sb_to_cstring: allocation failed");
    if (b.len > 0) {
        std::memcpy(out, b.data, static_cast<size_t>(b.len));
    }
    out[b.len] = '\0';
    return out;
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

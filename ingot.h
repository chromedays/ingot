#ifndef INGOT_H_
#define INGOT_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifndef ingot_assert_
#  include <cstdio>
#  include <cstdlib>
#  define ingot_assert_(cond, ...) \
       do { \
           if (!(cond)) { \
               std::fprintf(stderr, "ingot assert failed: " __VA_ARGS__); \
               std::fprintf(stderr, "\n  at %s:%d\n", __FILE__, __LINE__); \
               std::abort(); \
           } \
       } while (0)
#endif

namespace ingot {

class allocator_t {
public:
    virtual void* alloc(int64_t bytes, int64_t align) = 0;
    virtual void  free(void* ptr, int64_t bytes) = 0;
    virtual bool  resize(void* ptr, int64_t old_bytes, int64_t new_bytes, int64_t align) = 0;
    virtual ~allocator_t() = default;
};

class heap_allocator_t : public allocator_t {
public:
    void* alloc(int64_t bytes, int64_t align) override;
    void  free(void* ptr, int64_t bytes) override;
    bool  resize(void* ptr, int64_t old_bytes, int64_t new_bytes, int64_t align) override;
};

class arena_allocator_t : public allocator_t {
    void*   buffer_;
    int64_t offset_;
    int64_t size_;
public:
    void construct(void* backing_buffer, int64_t buffer_size);
    void destroy();
    void reset();

    void* alloc(int64_t bytes, int64_t align) override;
    void  free(void* ptr, int64_t bytes) override;
    bool  resize(void* ptr, int64_t old_bytes, int64_t new_bytes, int64_t align) override;
};

template<typename T>
struct static_vector_t {
    static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>,
                  "static_vector_t requires POD types");

    T*           data;
    int64_t      count;
    int64_t      capacity;
    allocator_t* alloc;

    T&       operator[](int64_t index)       { return data[index]; }
    const T& operator[](int64_t index) const { return data[index]; }
};

template<typename T>
void sv_create(static_vector_t<T>& v, allocator_t& a, int64_t capacity) {
    v.data = static_cast<T*>(a.alloc(capacity * static_cast<int64_t>(sizeof(T)),
                                     static_cast<int64_t>(alignof(T))));
    ingot_assert_(v.data != nullptr,
                  "sv_create: allocation failed (capacity=%lld, sizeof=%lld)",
                  capacity, static_cast<int64_t>(sizeof(T)));
    v.count    = 0;
    v.capacity = capacity;
    v.alloc    = &a;
}

template<typename T>
void sv_destroy(static_vector_t<T>& v) {
    if (v.data != nullptr) {
        v.alloc->free(v.data, v.capacity * static_cast<int64_t>(sizeof(T)));
    }
    v.data     = nullptr;
    v.count    = 0;
    v.capacity = 0;
    v.alloc    = nullptr;
}

template<typename T>
void sv_push(static_vector_t<T>& v, const T& value) {
    ingot_assert_(v.count < v.capacity,
                  "sv_push: overflow (count=%lld, capacity=%lld)",
                  v.count, v.capacity);
    v.data[v.count] = value;
    v.count++;
}

template<typename T>
void sv_pop(static_vector_t<T>& v) {
    ingot_assert_(v.count > 0,
                  "sv_pop: underflow (count=%lld)",
                  v.count);
    v.count--;
}

template<typename T>
void sv_clear(static_vector_t<T>& v) {
    v.count = 0;
}

template<typename T>
T* sv_begin(static_vector_t<T>& v) {
    return v.data;
}

template<typename T>
T* sv_end(static_vector_t<T>& v) {
    return v.data + v.count;
}

template<typename T>
const T* sv_begin(const static_vector_t<T>& v) {
    return v.data;
}

template<typename T>
const T* sv_end(const static_vector_t<T>& v) {
    return v.data + v.count;
}

template<typename T>
T* begin(static_vector_t<T>& v) { return sv_begin(v); }
template<typename T>
T* end(static_vector_t<T>& v)   { return sv_end(v); }
template<typename T>
const T* begin(const static_vector_t<T>& v) { return sv_begin(v); }
template<typename T>
const T* end(const static_vector_t<T>& v)   { return sv_end(v); }

template<typename T>
int64_t sv_count(const static_vector_t<T>& v) {
    return v.count;
}

template<typename T>
int64_t sv_capacity(const static_vector_t<T>& v) {
    return v.capacity;
}

template<typename T>
bool sv_empty(const static_vector_t<T>& v) {
    return v.count == 0;
}

template<typename T>
bool sv_full(const static_vector_t<T>& v) {
    return v.count >= v.capacity;
}

} // namespace ingot

#endif // INGOT_H_

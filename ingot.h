#ifndef INGOT_H_
#define INGOT_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
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
    int64_t total_bytes = capacity * static_cast<int64_t>(sizeof(T));
    ingot_assert_(total_bytes >= 0, "sv_create: capacity too large (overflow)");
    v.data = static_cast<T*>(a.alloc(total_bytes,
                                     static_cast<int64_t>(alignof(T))));
    ingot_assert_(v.data != nullptr,
                  "sv_create: allocation failed (capacity=%lld, sizeof=%lld)",
                  static_cast<long long>(capacity),
                  static_cast<long long>(sizeof(T)));
    v.count    = 0;
    v.capacity = capacity;
    v.alloc    = &a;
}

template<typename T>
void sv_destroy(static_vector_t<T>& v) {
    if (v.data != nullptr) {
        int64_t total_bytes = v.capacity * static_cast<int64_t>(sizeof(T));
        v.alloc->free(v.data, total_bytes);
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
                  static_cast<long long>(v.count),
                  static_cast<long long>(v.capacity));
    v.data[v.count] = value;
    v.count++;
}

template<typename T>
void sv_pop(static_vector_t<T>& v) {
    ingot_assert_(v.count > 0,
                  "sv_pop: underflow (count=%lld)",
                  static_cast<long long>(v.count));
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

// === 스트링 타입 ===

struct string_t {
    const char* data;
    int64_t     len;
};
static_assert(std::is_trivially_copyable_v<string_t> && std::is_standard_layout_v<string_t>,
              "string_t must be POD");

inline string_t str_from(const char* data, int64_t len) {
    ingot_assert_(len >= 0, "str_from: negative length (len=%lld)",
                  static_cast<long long>(len));
    ingot_assert_(data != nullptr || len == 0,
                  "str_from: null data with non-zero length (len=%lld)",
                  static_cast<long long>(len));
    return string_t{.data = data, .len = len};
}

template <int64_t N>
constexpr string_t str_lit(const char (&s)[N]) {
    return string_t{.data = s, .len = N - 1};
}

string_t str_from_cstr(const char* s);
bool     str_equal(string_t a, string_t b);

inline int64_t     str_len(string_t s)      { return s.len; }
inline bool        str_is_empty(string_t s) { return s.len == 0; }
inline const char* str_data(string_t s)     { return s.data; }

inline char str_at(string_t s, int64_t index) {
    ingot_assert_(index >= 0 && index < s.len,
                  "str_at: index out of range (index=%lld, len=%lld)",
                  static_cast<long long>(index), static_cast<long long>(s.len));
    return s.data[index];
}

inline string_t str_slice(string_t s, int64_t begin, int64_t end) {
    ingot_assert_(begin >= 0 && end >= begin && end <= s.len,
                  "str_slice: invalid range (begin=%lld, end=%lld, len=%lld)",
                  static_cast<long long>(begin),
                  static_cast<long long>(end),
                  static_cast<long long>(s.len));
    return string_t{.data = s.data + begin, .len = end - begin};
}

inline const char* str_begin(string_t s) { return s.data; }
inline const char* str_end(string_t s)   { return s.data + s.len; }
inline const char* begin(string_t s)     { return str_begin(s); }
inline const char* end(string_t s)       { return str_end(s); }

// === 스트링 빌더 ===

struct string_builder_t {
    char*        data;
    int64_t      len;
    int64_t      capacity;
    allocator_t* alloc;
};
static_assert(std::is_trivially_copyable_v<string_builder_t> && std::is_standard_layout_v<string_builder_t>,
              "string_builder_t must be POD");

void sb_create(string_builder_t& b, allocator_t& a, int64_t initial_capacity);
void sb_destroy(string_builder_t& b);

inline int64_t sb_len(const string_builder_t& b)      { return b.len; }
inline int64_t sb_capacity(const string_builder_t& b) { return b.capacity; }
inline bool    sb_is_empty(const string_builder_t& b) { return b.len == 0; }
inline const char* sb_data(const string_builder_t& b) { return b.data; }
inline char*       sb_data(string_builder_t& b)       { return b.data; }

inline char sb_at(const string_builder_t& b, int64_t index) {
    ingot_assert_(index >= 0 && index < b.len,
                  "sb_at: index out of range (index=%lld, len=%lld)",
                  static_cast<long long>(index), static_cast<long long>(b.len));
    return b.data[index];
}

inline void sb_clear(string_builder_t& b) { b.len = 0; }

inline void sb_truncate(string_builder_t& b, int64_t new_len) {
    ingot_assert_(new_len >= 0 && new_len <= b.len,
                  "sb_truncate: invalid new_len (new_len=%lld, len=%lld)",
                  static_cast<long long>(new_len), static_cast<long long>(b.len));
    b.len = new_len;
}

inline void sb_pop(string_builder_t& b) {
    ingot_assert_(b.len > 0, "sb_pop: underflow (len=0)");
    b.len--;
}

void sb_append_bytes(string_builder_t& b, const char* p, int64_t n);
void sb_append(string_builder_t& b, char c);
void sb_append(string_builder_t& b, const char* s);
void sb_append(string_builder_t& b, string_t v);
void sb_reserve(string_builder_t& b, int64_t total_capacity);
char* sb_to_cstring(const string_builder_t& b, allocator_t& alloc);

inline string_t sb_to_string(const string_builder_t& b) {
    return string_t{.data = b.data, .len = b.len};
}

// === 스태틱 스트링 빌더 (스택 소유자, 하드 캡) ===

template <int64_t N>
struct static_string_builder_t {
    static_assert(N > 0, "static_string_builder_t<N>: N must be > 0");
    char    buffer[N];
    int64_t len;
};
static_assert(std::is_trivially_copyable_v<static_string_builder_t<16>> &&
              std::is_standard_layout_v<static_string_builder_t<16>>,
              "static_string_builder_t must be POD");

template <int64_t N>
inline void ssb_create(static_string_builder_t<N>& b) {
    b.len = 0;
}

template <int64_t N>
inline int64_t ssb_capacity(const static_string_builder_t<N>&) { return N; }

template <int64_t N>
inline int64_t ssb_len(const static_string_builder_t<N>& b) { return b.len; }

template <int64_t N>
inline bool ssb_is_empty(const static_string_builder_t<N>& b) { return b.len == 0; }

template <int64_t N>
inline bool ssb_is_full(const static_string_builder_t<N>& b) { return b.len >= N; }

template <int64_t N>
inline const char* ssb_data(const static_string_builder_t<N>& b) { return b.buffer; }

template <int64_t N>
inline char* ssb_data(static_string_builder_t<N>& b) { return b.buffer; }

template <int64_t N>
inline string_t ssb_to_string(const static_string_builder_t<N>& b) {
    return string_t{.data = b.buffer, .len = b.len};
}

template <int64_t N>
inline void ssb_append_bytes(static_string_builder_t<N>& b, const char* p, int64_t n) {
    ingot_assert_(n >= 0, "ssb_append_bytes: negative length (n=%lld)",
                  static_cast<long long>(n));
    ingot_assert_(b.len + n <= N,
                  "ssb_append_bytes: overflow (len=%lld, add=%lld, capacity=%lld)",
                  static_cast<long long>(b.len), static_cast<long long>(n),
                  static_cast<long long>(N));
    if (n == 0) return;
    ingot_assert_(p != nullptr, "ssb_append_bytes: null pointer with n>0");
    std::memcpy(b.buffer + b.len, p, static_cast<size_t>(n));
    b.len += n;
}

template <int64_t N>
inline void ssb_append(static_string_builder_t<N>& b, char c) {
    ingot_assert_(b.len < N,
                  "ssb_append: overflow (char) (len=%lld, capacity=%lld)",
                  static_cast<long long>(b.len), static_cast<long long>(N));
    b.buffer[b.len] = c;
    b.len++;
}

template <int64_t N>
inline void ssb_append(static_string_builder_t<N>& b, const char* s) {
    ingot_assert_(s != nullptr, "ssb_append: null pointer");
    ssb_append_bytes(b, s, static_cast<int64_t>(std::strlen(s)));
}

template <int64_t N>
inline void ssb_append(static_string_builder_t<N>& b, string_t v) {
    ssb_append_bytes(b, v.data, v.len);
}

template <int64_t N>
inline char ssb_at(const static_string_builder_t<N>& b, int64_t index) {
    ingot_assert_(index >= 0 && index < b.len,
                  "ssb_at: index out of range (index=%lld, len=%lld)",
                  static_cast<long long>(index), static_cast<long long>(b.len));
    return b.buffer[index];
}

template <int64_t N>
inline void ssb_clear(static_string_builder_t<N>& b) { b.len = 0; }

template <int64_t N>
inline void ssb_truncate(static_string_builder_t<N>& b, int64_t new_len) {
    ingot_assert_(new_len >= 0 && new_len <= b.len,
                  "ssb_truncate: invalid new_len (new_len=%lld, len=%lld)",
                  static_cast<long long>(new_len), static_cast<long long>(b.len));
    b.len = new_len;
}

template <int64_t N>
inline void ssb_pop(static_string_builder_t<N>& b) {
    ingot_assert_(b.len > 0, "ssb_pop: underflow (len=0)");
    b.len--;
}

template <int64_t N>
inline char* ssb_to_cstring(const static_string_builder_t<N>& b, allocator_t& alloc) {
    char* out = static_cast<char*>(alloc.alloc(b.len + 1, 1));
    ingot_assert_(out != nullptr, "ssb_to_cstring: allocation failed");
    if (b.len > 0) {
        std::memcpy(out, b.buffer, static_cast<size_t>(b.len));
    }
    out[b.len] = '\0';
    return out;
}

// === UTF-8 헬퍼 (옵션) ===

inline constexpr char32_t utf8_rune_error = 0xFFFD;

char32_t utf8_decode_rune(const char* p, int64_t remaining, int* out_width);
int     utf8_encode_rune(char32_t rune, char* out_buf);
bool    utf8_validate(string_t s);
int64_t utf8_rune_count(string_t s);

struct utf8_rune_view_t {
    string_t source;
};
static_assert(std::is_trivially_copyable_v<utf8_rune_view_t> &&
              std::is_standard_layout_v<utf8_rune_view_t>,
              "utf8_rune_view_t must be POD");

struct utf8_rune_cursor_t {
    const char* p;
    const char* end;
    char32_t    current;
    int         width;
};
static_assert(std::is_trivially_copyable_v<utf8_rune_cursor_t> &&
              std::is_standard_layout_v<utf8_rune_cursor_t>,
              "utf8_rune_cursor_t must be POD");

inline utf8_rune_view_t utf8_runes(string_t s) {
    return utf8_rune_view_t{.source = s};
}

inline utf8_rune_cursor_t begin(utf8_rune_view_t v) {
    utf8_rune_cursor_t c;
    c.p   = v.source.data;
    c.end = v.source.data + v.source.len;
    if (c.p < c.end) {
        int width;
        c.current = utf8_decode_rune(c.p, static_cast<int64_t>(c.end - c.p), &width);
        c.width   = width;
    } else {
        c.current = 0;
        c.width   = 0;
    }
    return c;
}

inline utf8_rune_cursor_t end(utf8_rune_view_t v) {
    utf8_rune_cursor_t c;
    c.p       = v.source.data + v.source.len;
    c.end     = c.p;
    c.current = 0;
    c.width   = 0;
    return c;
}

inline char32_t operator*(utf8_rune_cursor_t c) {
    return c.current;
}

inline utf8_rune_cursor_t& operator++(utf8_rune_cursor_t& c) {
    c.p += c.width;
    if (c.p < c.end) {
        int width;
        c.current = utf8_decode_rune(c.p, static_cast<int64_t>(c.end - c.p), &width);
        c.width   = width;
    } else {
        c.current = 0;
        c.width   = 0;
    }
    return c;
}

inline bool operator!=(utf8_rune_cursor_t a, utf8_rune_cursor_t b) {
    return a.p != b.p;
}

} // namespace ingot

#endif // INGOT_H_

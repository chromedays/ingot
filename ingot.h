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

template<typename T>
inline constexpr bool is_pod = std::is_trivially_copyable_v<T> &&
                               std::is_standard_layout_v<T>;

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
    static_assert(is_pod<T>,
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

// === 뷰 타입 ===

template<typename T>
struct view_t {
    static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>,
                  "view_t<T> requires POD types");

    T*      data;
    int64_t len;
};
static_assert(std::is_trivially_copyable_v<view_t<int>> &&
              std::is_standard_layout_v<view_t<int>>,
              "view_t must be POD");

template<typename T>
view_t<T> view_from(T* data, int64_t len) {
    ingot_assert_(len >= 0, "view_from: negative length (len=%lld)",
                  static_cast<long long>(len));
    ingot_assert_(data != nullptr || len == 0,
                  "view_from: null data with non-zero length (len=%lld)",
                  static_cast<long long>(len));
    return view_t<T>{.data = data, .len = len};
}

template<typename T, int64_t N>
view_t<T> view_from(T (&arr)[N]) {
    return view_t<T>{.data = arr, .len = N};
}

template<typename T>
view_t<T> view_from(static_vector_t<T>& sv) {
    return view_t<T>{.data = sv.data, .len = sv.count};
}

template<typename T>
view_t<const T> view_from(const static_vector_t<T>& sv) {
    return view_t<const T>{.data = sv.data, .len = sv.count};
}

template<typename T>
int64_t view_len(view_t<T> v) {
    return v.len;
}

template<typename T>
bool view_is_empty(view_t<T> v) {
    return v.len == 0;
}

template<typename T>
T* view_data(view_t<T> v) {
    return v.data;
}

template<typename T>
T& view_at(view_t<T> v, int64_t index) {
    ingot_assert_(index >= 0 && index < v.len,
                  "view_at: index out of range (index=%lld, len=%lld)",
                  static_cast<long long>(index),
                  static_cast<long long>(v.len));
    return v.data[index];
}

template<typename T>
view_t<T> view_slice(view_t<T> v, int64_t low, int64_t high) {
    ingot_assert_(low >= 0 && high >= low && high <= v.len,
                  "view_slice: invalid range (low=%lld, high=%lld, len=%lld)",
                  static_cast<long long>(low),
                  static_cast<long long>(high),
                  static_cast<long long>(v.len));
    return view_t<T>{.data = v.data + low, .len = high - low};
}

template<typename T>
T* view_begin(view_t<T> v) {
    return v.data;
}

template<typename T>
T* view_end(view_t<T> v) {
    return v.data + v.len;
}

template<typename T>
T* begin(view_t<T> v) {
    return view_begin(v);
}

template<typename T>
T* end(view_t<T> v) {
    return view_end(v);
}

// === 스트링 타입 ===

struct string_t {
    const char* data;
    int64_t     len;
};
static_assert(is_pod<string_t>,
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

constexpr string_t operator""_str(const char* s, std::size_t len) {
    return string_t{.data = s, .len = static_cast<int64_t>(len)};
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
static_assert(is_pod<string_builder_t>,
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
static_assert(is_pod<static_string_builder_t<16>>,
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
static_assert(is_pod<utf8_rune_view_t>,
              "utf8_rune_view_t must be POD");

struct utf8_rune_cursor_t {
    const char* p;
    const char* end;
    char32_t    current;
    int         width;
};
static_assert(is_pod<utf8_rune_cursor_t>,
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

// === 스트링 포맷 ===

template <typename T>
struct format_traits {
    template <typename B>
    static void write(B& b, const T& val) {
        static_assert(sizeof(T) == 0,
            "No formatter registered for this type. "
            "Use format_register_(Type, fmt_str, ...) to register one.");
    }
};

// 동적/정적 빌더의 포맷 API 차이를 숨겨 format_traits에서
// 빌더 타입과 무관하게 재귀 포매팅할 수 있게 한다.
template <typename... Args>
void format_write(string_builder_t& b, string_t fmt, Args&&... args);

template <int64_t N, typename... Args>
void format_write(static_string_builder_t<N>& b, string_t fmt, Args&&... args);

namespace detail {

template <int64_t N>
string_t format_u64(char (&buf)[N], uint64_t val) {
    if (val == 0) {
        buf[0] = '0';
        return str_from(buf, 1);
    }
    int64_t pos = N;
    while (val > 0) {
        pos--;
        buf[pos] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    return str_from(buf + pos, N - pos);
}

template <int64_t N>
string_t format_i64(char (&buf)[N], int64_t val) {
    if (val >= 0) {
        return format_u64(buf, static_cast<uint64_t>(val));
    }
    if (val == INT64_MIN) {
        const char* lit = "-9223372036854775808";
        constexpr int64_t len = 20;
        std::memcpy(buf, lit, static_cast<size_t>(len));
        return str_from(buf, len);
    }
    uint64_t abs_val = static_cast<uint64_t>(-val);
    string_t digits = format_u64(buf, abs_val);
    int64_t sign_pos = (digits.data - buf) - 1;
    ingot_assert_(sign_pos >= 0, "format_i64: buffer too small for sign");
    buf[sign_pos] = '-';
    return str_from(buf + sign_pos, digits.len + 1);
}

inline int64_t count_placeholders(string_t fmt) {
    int64_t count = 0;
    for (int64_t i = 0; i < fmt.len; ++i) {
        if (fmt.data[i] == '{' && i + 1 < fmt.len) {
            if (fmt.data[i + 1] == '{') {
                i++;
            } else if (fmt.data[i + 1] == '}') {
                count++;
                i++;
            }
        }
    }
    return count;
}

template <typename T>
void format_one_sb(string_builder_t& b, const void* ptr) {
    const T& val = *static_cast<const T*>(ptr);
    if constexpr (std::is_same_v<T, string_t>) {
        sb_append(b, val);
    } else if constexpr (std::is_same_v<T, const char*>) {
        sb_append(b, val);
    } else if constexpr (std::is_same_v<T, char>) {
        sb_append(b, val);
    } else if constexpr (std::is_same_v<T, bool>) {
        sb_append(b, val ? "true" : "false");
    } else if constexpr (std::is_same_v<T, char32_t>) {
        char rune_buf[4];
        int width = utf8_encode_rune(val, rune_buf);
        ingot_assert_(width > 0, "sb_format: invalid char32_t rune (U+%04X)",
                      static_cast<unsigned>(val));
        sb_append_bytes(b, rune_buf, static_cast<int64_t>(width));
    } else if constexpr (std::is_floating_point_v<T>) {
        char fbuf[64];
        int len = std::snprintf(fbuf, sizeof(fbuf), "%g", static_cast<double>(val));
        ingot_assert_(len > 0, "sb_format: snprintf failed");
        sb_append_bytes(b, fbuf, static_cast<int64_t>(len));
    } else if constexpr (std::is_integral_v<T>) {
        char nbuf[32];
        string_t s;
        if constexpr (std::is_signed_v<T>) {
            s = format_i64(nbuf, static_cast<int64_t>(val));
        } else {
            s = format_u64(nbuf, static_cast<uint64_t>(val));
        }
        sb_append(b, s);
    } else {
        format_traits<T>::write(b, val);
    }
}

template <int64_t N, typename T>
void format_one_ssb(static_string_builder_t<N>& b, const void* ptr) {
    const T& val = *static_cast<const T*>(ptr);
    if constexpr (std::is_same_v<T, string_t>) {
        ssb_append(b, val);
    } else if constexpr (std::is_same_v<T, const char*>) {
        ssb_append(b, val);
    } else if constexpr (std::is_same_v<T, char>) {
        ssb_append(b, val);
    } else if constexpr (std::is_same_v<T, bool>) {
        ssb_append(b, val ? "true" : "false");
    } else if constexpr (std::is_same_v<T, char32_t>) {
        char rune_buf[4];
        int width = utf8_encode_rune(val, rune_buf);
        ingot_assert_(width > 0, "ssb_format: invalid char32_t rune (U+%04X)",
                      static_cast<unsigned>(val));
        ssb_append_bytes(b, rune_buf, static_cast<int64_t>(width));
    } else if constexpr (std::is_floating_point_v<T>) {
        char fbuf[64];
        int len = std::snprintf(fbuf, sizeof(fbuf), "%g", static_cast<double>(val));
        ingot_assert_(len > 0, "ssb_format: snprintf failed");
        ssb_append_bytes(b, fbuf, static_cast<int64_t>(len));
    } else if constexpr (std::is_integral_v<T>) {
        char nbuf[32];
        string_t s;
        if constexpr (std::is_signed_v<T>) {
            s = format_i64(nbuf, static_cast<int64_t>(val));
        } else {
            s = format_u64(nbuf, static_cast<uint64_t>(val));
        }
        ssb_append(b, s);
    } else {
        format_traits<T>::write(b, val);
    }
}

} // namespace detail

#define format_register_(Type, fmt_str, ...)                              \
    template <>                                                            \
    struct ::ingot::format_traits<Type> {                                  \
        template <typename _B>                                             \
        static void write(_B& b, const Type& _v) {                        \
            format_write(b, ::ingot::str_lit(fmt_str),                    \
                         __VA_ARGS__);                                     \
        }                                                                  \
    };

template <typename... Args>
void sb_format(string_builder_t& b, string_t fmt, Args... args) {
    int64_t n = detail::count_placeholders(fmt);
    ingot_assert_(n == static_cast<int64_t>(sizeof...(Args)),
                  "sb_format: placeholder/argument count mismatch "
                  "(placeholders=%lld, args=%lld)",
                  static_cast<long long>(n),
                  static_cast<long long>(sizeof...(Args)));

    if constexpr (sizeof...(Args) == 0) {
        const char* p = fmt.data;
        const char* end = fmt.data + fmt.len;
        while (p < end) {
            if (p[0] == '{' && p + 1 < end) {
                if (p[1] == '{') {
                    sb_append(b, '{');
                    p += 2;
                } else if (p[1] == '}') {
                    ingot_assert_(false, "sb_format: placeholder without matching argument");
                } else {
                    ingot_assert_(false, "sb_format: unexpected '{' (not '{{' or '{}')");
                }
            } else if (p[0] == '}' && p + 1 < end && p[1] == '}') {
                sb_append(b, '}');
                p += 2;
            } else if (p[0] == '}') {
                ingot_assert_(false, "sb_format: unexpected '}' (not '}}')");
            } else {
                sb_append(b, *p);
                p++;
            }
        }
        return;
    }

    using fn_t = void(*)(string_builder_t&, const void*);
    fn_t fns[] = { &detail::format_one_sb<std::decay_t<Args>>... };
    const void* ptrs[] = { static_cast<const void*>(&args)... };

    const char* p = fmt.data;
    const char* end = fmt.data + fmt.len;
    int64_t idx = 0;

    while (p < end) {
        if (p[0] == '{' && p + 1 < end) {
            if (p[1] == '{') {
                sb_append(b, '{');
                p += 2;
            } else if (p[1] == '}') {
                fns[idx](b, ptrs[idx]);
                idx++;
                p += 2;
            } else {
                ingot_assert_(false, "sb_format: unexpected '{' (not '{{' or '{}')");
            }
        } else if (p[0] == '}' && p + 1 < end && p[1] == '}') {
            sb_append(b, '}');
            p += 2;
        } else if (p[0] == '}') {
            ingot_assert_(false, "sb_format: unexpected '}' (not '}}')");
        } else {
            sb_append(b, *p);
            p++;
        }
    }
}

template <int64_t N, typename... Args>
void ssb_format(static_string_builder_t<N>& b, string_t fmt, Args... args) {
    int64_t n = detail::count_placeholders(fmt);
    ingot_assert_(n == static_cast<int64_t>(sizeof...(Args)),
                  "ssb_format: placeholder/argument count mismatch "
                  "(placeholders=%lld, args=%lld)",
                  static_cast<long long>(n),
                  static_cast<long long>(sizeof...(Args)));

    if constexpr (sizeof...(Args) == 0) {
        const char* p = fmt.data;
        const char* end = fmt.data + fmt.len;
        while (p < end) {
            if (p[0] == '{' && p + 1 < end) {
                if (p[1] == '{') {
                    ssb_append(b, '{');
                    p += 2;
                } else if (p[1] == '}') {
                    ingot_assert_(false, "ssb_format: placeholder without matching argument");
                } else {
                    ingot_assert_(false, "ssb_format: unexpected '{' (not '{{' or '{}')");
                }
            } else if (p[0] == '}' && p + 1 < end && p[1] == '}') {
                ssb_append(b, '}');
                p += 2;
            } else if (p[0] == '}') {
                ingot_assert_(false, "ssb_format: unexpected '}' (not '}}')");
            } else {
                ssb_append(b, *p);
                p++;
            }
        }
        return;
    }

    using fn_t = void(*)(static_string_builder_t<N>&, const void*);
    fn_t fns[] = { &detail::format_one_ssb<N, std::decay_t<Args>>... };
    const void* ptrs[] = { static_cast<const void*>(&args)... };

    const char* p = fmt.data;
    const char* end = fmt.data + fmt.len;
    int64_t idx = 0;

    while (p < end) {
        if (p[0] == '{' && p + 1 < end) {
            if (p[1] == '{') {
                ssb_append(b, '{');
                p += 2;
            } else if (p[1] == '}') {
                fns[idx](b, ptrs[idx]);
                idx++;
                p += 2;
            } else {
                ingot_assert_(false, "ssb_format: unexpected '{' (not '{{' or '{}')");
            }
        } else if (p[0] == '}' && p + 1 < end && p[1] == '}') {
            ssb_append(b, '}');
            p += 2;
        } else if (p[0] == '}') {
            ingot_assert_(false, "ssb_format: unexpected '}' (not '}}')");
        } else {
            ssb_append(b, *p);
            p++;
        }
    }
}

template <typename... Args>
void format_write(string_builder_t& b, string_t fmt, Args&&... args) {
    sb_format(b, fmt, static_cast<Args&&>(args)...);
}

template <int64_t N, typename... Args>
void format_write(static_string_builder_t<N>& b, string_t fmt, Args&&... args) {
    ssb_format(b, fmt, static_cast<Args&&>(args)...);
}

template <typename T>
struct format_traits<static_vector_t<T>> {
    template <typename B>
    static void write(B& b, const static_vector_t<T>& v) {
        format_write(b, str_lit("["));
        int64_t n = sv_count(v);
        for (int64_t i = 0; i < n; ++i) {
            if (i > 0) format_write(b, str_lit(", "));
            format_write(b, str_lit("{}"), v[i]);
        }
        format_write(b, str_lit("]"));
    }
};

template <typename T>
struct format_traits<view_t<T>> {
    template <typename B>
    static void write(B& b, view_t<T> v) {
        format_write(b, str_lit("["));
        int64_t n = view_len(v);
        for (int64_t i = 0; i < n; ++i) {
            if (i > 0) format_write(b, str_lit(", "));
            format_write(b, str_lit("{}"), view_at(v, i));
        }
        format_write(b, str_lit("]"));
    }
};

template <>
struct format_traits<string_t> {
    template <typename B>
    static void write(B& b, string_t val) {
        format_write(b, str_lit("{}"), val);
    }
};

template <>
struct format_traits<string_builder_t> {
    template <typename B>
    static void write(B& b, const string_builder_t& sb) {
        format_write(b, str_lit("{}"), sb_to_string(sb));
        format_write(b, str_lit(" [{}/{}]"), sb_len(sb), sb_capacity(sb));
    }
};

template <int64_t N>
struct format_traits<static_string_builder_t<N>> {
    template <typename B>
    static void write(B& b, const static_string_builder_t<N>& sb) {
        format_write(b, str_lit("{}"), ssb_to_string(sb));
        format_write(b, str_lit(" [{}/{}]"), ssb_len(sb), N);
    }
};

template <>
struct format_traits<utf8_rune_view_t> {
    template <typename B>
    static void write(B& b, utf8_rune_view_t v) {
        for (auto c = begin(v); c != end(v); ++c) {
            format_write(b, str_lit("{}"), *c);
        }
    }
};

} // namespace ingot

#endif // INGOT_H_

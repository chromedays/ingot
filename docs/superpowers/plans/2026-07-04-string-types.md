# ingot 스트링 타입 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `ingot` 라이브러리에 뷰-퍼스트 스트링 타입 3종(`string_t`, `string_builder_t`, `static_string_builder_t<N>`)과 옵션 UTF-8 헬퍼 모듈을 추가한다.

**Architecture:** 비소유 뷰 `string_t {const char* data; int64_t len}` 을 주 타입으로, 소유/확장 가능한 `string_builder_t`(할당자 기반, 2배 성장), 고정 용량 `static_string_builder_t<N>`(인라인 버퍼, 하드 캡)을 동반 타입으로 둔다. NUL 종료 없는 순수 바이트 의미론(Option 2). UTF-8은 관례, 검증/디코드는 옵션. 자유 함수 + 타입 접두사(`str_`/`sb_`/`ssb_`/`utf8_`) 규칙. 모든 코드는 기존 `ingot.h`/`ingot.cpp` 단일 파일에 추가.

**Tech Stack:** C++23, CMake, doctest v2.5.2 (vendored), CTest.

**참조 스펙:** [docs/superpowers/specs/2026-07-04-string-types-design.md](../specs/2026-07-04-string-types-design.md)

---

## File Structure

| 파일 | 작업 | 책임 |
|---|---|---|
| `ingot.h` | Modify | 스트링 타입 3종 + UTF-8 헬퍼 선언/인라인 정의 (템플릿, 접근자) |
| `ingot.cpp` | Modify | 비템플릿 구현 (`str_from_cstr`, `str_equal`, `sb_*` 본체, `utf8_*` 본체) |
| `tests/doctest_main.cpp` | Create | `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` 인라인 — `main()` 자동 생성 |
| `tests/test_static_vector.cpp` | Modify | 최상단 매크로/`#include "doctest.h"` 제거 (분리된 TU로 전환) |
| `tests/test_string.cpp` | Create | 스트링 타입 + UTF-8 doctest 테스트 |
| `tests/CMakeLists.txt` | Modify | `test_string` 실행 파일 추가, 두 바이너리 모두 `doctest_main.cpp` 링크 |

---

## Task 1: 테스트 인프라 분리 (`doctest_main.cpp`)

스트링 테스트를 별도 TU(`test_string.cpp`)로 추가하려면 `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` 이 단一处에만 있어야 한다. doctest 스펙이 예견한 대로 `tests/doctest_main.cpp` 로 분리한다.

**Files:**
- Create: `tests/doctest_main.cpp`
- Modify: `tests/test_static_vector.cpp` (최상단 2줄 제거)
- Create: `tests/test_string.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: `tests/doctest_main.cpp` 생성**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 2: `tests/test_static_vector.cpp` 최상단 수정**

기존 최상단 2줄을 제거한다:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

```

제거 후 파일은 `#include "ingot.h"` 로 시작해야 한다. 나머지 `TEST_CASE` 들은 그대로 둔다.

- [ ] **Step 3: `tests/test_string.cpp` 스모크 케이스 생성**

```cpp
#include "doctest.h"

#include "ingot.h"

TEST_CASE("string types smoke") {
    CHECK(true);
}
```

- [ ] **Step 4: `tests/CMakeLists.txt` 수정**

기존 내용을 다음으로 교체:

```cmake
add_executable(test_static_vector test_static_vector.cpp doctest_main.cpp)
target_link_libraries(test_static_vector PRIVATE ingot)
add_test(NAME test_static_vector COMMAND test_static_vector)

add_executable(test_string test_string.cpp doctest_main.cpp)
target_link_libraries(test_string PRIVATE ingot)
add_test(NAME test_string COMMAND test_string)
```

- [ ] **Step 5: 빌드 + 기존 테스트 회귀 확인**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `test_static_vector` 전 케이스 통과(기존 11개), `test_string` 의 smoke 케이스 통과. 두 바이너리 모두 `main()` 중복 없이 링크 성공.

- [ ] **Step 6: 커밋**

```bash
git add tests/doctest_main.cpp tests/test_static_vector.cpp tests/test_string.cpp tests/CMakeLists.txt
git commit -m "Split doctest main into doctest_main.cpp for multi-TU tests

Extract DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN from test_static_vector.cpp
into a shared tests/doctest_main.cpp so multiple test TUs can link
against one main(). Add empty test_string.cpp smoke test and register
both test binaries in CMake."
```

---

## Task 2: `string_t` — 생성 + 검사

뷰 타입 `string_t` 레이아웃과 생성/검사 함수 추가. 헤더에 `<cstring>` 포함 추가(str 후속 태스크에서 필요).

**Files:**
- Modify: `ingot.h` (`<cstring>` 포함 + `namespace ingot` 내 `string_t` 섹션 추가)
- Modify: `ingot.cpp` (`str_from_cstr` 구현)
- Modify: `tests/test_string.cpp`

- [ ] **Step 1: 실패 테스트 작성**

`tests/test_string.cpp` 의 smoke 케이스 뒤에 추가:

```cpp
TEST_CASE("string_t: str_from") {
    const char* p = "hello";
    ingot::string_t s = ingot::str_from(p, 5);
    CHECK_MESSAGE(ingot::str_len(s) == 5, "len should be 5");
    CHECK_MESSAGE(ingot::str_data(s) == p, "data should point to source");
    CHECK_MESSAGE(!ingot::str_is_empty(s), "should not be empty");
}

TEST_CASE("string_t: str_from_cstr") {
    ingot::string_t s = ingot::str_from_cstr("hello");
    CHECK_MESSAGE(ingot::str_len(s) == 5, "len via strlen");
    CHECK_MESSAGE(ingot::str_data(s)[0] == 'h', "first byte");
    CHECK_MESSAGE(ingot::str_data(s)[4] == 'o', "last byte");
}

TEST_CASE("string_t: str_lit constexpr") {
    constexpr ingot::string_t s = ingot::str_lit("hello");
    static_assert(s.len == 5, "str_lit length must be constexpr");
    CHECK_MESSAGE(ingot::str_len(s) == 5, "literal len excludes NUL");
}

TEST_CASE("string_t: str_at") {
    ingot::string_t s = ingot::str_from_cstr("abc");
    CHECK_MESSAGE(ingot::str_at(s, 0) == 'a', "at 0");
    CHECK_MESSAGE(ingot::str_at(s, 2) == 'c', "at 2");
}

TEST_CASE("string_t: empty view") {
    ingot::string_t e = ingot::str_from("", 0);
    CHECK_MESSAGE(ingot::str_is_empty(e), "empty");
    CHECK_MESSAGE(ingot::str_len(e) == 0, "len 0");
}
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`str_from`, `string_t` 등 정의 없음).

- [ ] **Step 3: `ingot.h` — `<cstring>` 포함 + `string_t` 섹션 추가**

`ingot.h` 상단 기존 include 들 뒤에 `<cstring>` 추가:

```cpp
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
```

`namespace ingot {` 닫는 `}` 직전(기존 `static_vector_t` 섹션 뒤)에 다음 섹션 추가:

```cpp
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
    return string_t{.data = data, .len = len};
}

template <int64_t N>
constexpr string_t str_lit(const char (&s)[N]) {
    return string_t{.data = s, .len = N - 1};
}

string_t str_from_cstr(const char* s);

inline int64_t     str_len(string_t s)      { return s.len; }
inline bool        str_is_empty(string_t s) { return s.len == 0; }
inline const char* str_data(string_t s)     { return s.data; }

inline char str_at(string_t s, int64_t index) {
    ingot_assert_(index >= 0 && index < s.len,
                  "str_at: index out of range (index=%lld, len=%lld)",
                  static_cast<long long>(index), static_cast<long long>(s.len));
    return s.data[index];
}
```

- [ ] **Step 4: `ingot.cpp` — `str_from_cstr` 구현**

`ingot.cpp` 상단 include 순서 확인 (`ingot.h` → 표준 → 프로젝트). `<cstring>` 은 `ingot.h` 를 통해 이미 포함됨. `namespace ingot {` 내에 추가:

```cpp
string_t str_from_cstr(const char* s) {
    ingot_assert_(s != nullptr, "str_from_cstr: null pointer");
    return string_t{.data = s, .len = static_cast<int64_t>(std::strlen(s))};
}
```

- [ ] **Step 5: 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 신규 5개 케이스 + smoke 모두 통과.

- [ ] **Step 6: 커밋**

```bash
git add ingot.h ingot.cpp tests/test_string.cpp
git commit -m "Add string_t view type with construction and inspection

Non-owning (const char*, len) view as the primary string type.
str_from/str_from_cstr/str_lit construction, len/is_empty/data/at
inspection. POD-validated. Bytes-only, no NUL guarantees."
```

---

## Task 3: `string_t` — 비교, 슬라이스, 바이트 반복

뷰의 읽기 연산(비교/슬라이스/반복) 추가.

**Files:**
- Modify: `ingot.h`
- Modify: `ingot.cpp`
- Modify: `tests/test_string.cpp`

- [ ] **Step 1: 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("string_t: str_equal") {
    ingot::string_t a = ingot::str_from_cstr("hello");
    ingot::string_t b = ingot::str_from_cstr("hello");
    ingot::string_t c = ingot::str_from_cstr("world");
    CHECK_MESSAGE(ingot::str_equal(a, b), "equal contents");
    CHECK_MESSAGE(!ingot::str_equal(a, c), "different contents");
    CHECK_MESSAGE(ingot::str_equal(ingot::str_from("", 0), ingot::str_from("", 0)),
                  "two empty views equal");
    CHECK_MESSAGE(!ingot::str_equal(ingot::str_from_cstr("hi"), ingot::str_from_cstr("hi!")),
                  "different lengths not equal");
}

TEST_CASE("string_t: str_slice") {
    ingot::string_t s = ingot::str_from_cstr("hello world");
    ingot::string_t sub = ingot::str_slice(s, 6, 11);
    CHECK_MESSAGE(ingot::str_equal(sub, ingot::str_from_cstr("world")), "substring");
    CHECK_MESSAGE(ingot::str_len(sub) == 5, "substring len");
    ingot::string_t empty_slice = ingot::str_slice(s, 0, 0);
    CHECK_MESSAGE(ingot::str_is_empty(empty_slice), "zero-width slice");
    ingot::string_t full_slice = ingot::str_slice(s, 0, 11);
    CHECK_MESSAGE(ingot::str_equal(full_slice, s), "full slice equals source");
}

TEST_CASE("string_t: byte range-for") {
    ingot::string_t s = ingot::str_from_cstr("abc");
    const char expected[] = {'a', 'b', 'c'};
    int count = 0;
    for (char c : s) {
        CHECK_MESSAGE(c == expected[count], "byte mismatch");
        count++;
    }
    CHECK_MESSAGE(count == 3, "should visit 3 bytes");
}
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`str_equal`, `str_slice`, `begin`/`end` 정의 없음).

- [ ] **Step 3: `ingot.h` — `str_slice` + 반복자 추가**

`string_t` 섹션(`str_at` 뒤)에 추가:

```cpp
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
```

그리고 `str_equal` 선언을 `string_t` 섹션에 추가(`str_from_cstr` 선언 근처):

```cpp
bool str_equal(string_t a, string_t b);
```

- [ ] **Step 4: `ingot.cpp` — `str_equal` 구현**

`namespace ingot {` 내 `str_from_cstr` 뒤에 추가:

```cpp
bool str_equal(string_t a, string_t b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return std::memcmp(a.data, b.data, static_cast<size_t>(a.len)) == 0;
}
```

- [ ] **Step 5: 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 신규 3개 케이스 통과.

- [ ] **Step 6: 커밋**

```bash
git add ingot.h ingot.cpp tests/test_string.cpp
git commit -m "Add string_t comparison, slicing, and byte iteration

str_equal (memcmp), str_slice (zero-copy sub-view), and ADL begin/end
for byte-level range-for."
```

---

## Task 4: `string_builder_t` — 라이프사이클, 추가, 성장, 접근, 변형, 추출

힙 소유자 전체. 2배 기하급수적 성장, 지연 할당, `allocator_t*` 기반. 섹션이 크므로 TDD 사이클을 여러 번 반복한다.

**Files:**
- Modify: `ingot.h`
- Modify: `ingot.cpp`
- Modify: `tests/test_string.cpp`

- [ ] **Step 1: create/destroy 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("string_builder_t: create/destroy") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 16);
    REQUIRE_MESSAGE(b.data != nullptr, "data should not be null after create");
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "len 0 after create");
    CHECK_MESSAGE(ingot::sb_capacity(b) == 16, "capacity preserved");
    CHECK_MESSAGE(b.alloc == &heap, "alloc stored");
    ingot::sb_destroy(b);
    CHECK_MESSAGE(b.data == nullptr, "data null after destroy");
    CHECK_MESSAGE(b.alloc == nullptr, "alloc cleared");
}

TEST_CASE("string_builder_t: double destroy safe") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 4);
    ingot::sb_destroy(b);
    ingot::sb_destroy(b);
    CHECK_MESSAGE(b.data == nullptr, "still null");
}

TEST_CASE("string_builder_t: lazy allocation") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    CHECK_MESSAGE(b.data == nullptr, "no alloc when capacity=0");
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "len 0");
    ingot::sb_destroy(b);
}
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`string_builder_t`, `sb_create` 등 정의 없음).

- [ ] **Step 3: `ingot.h` — `string_builder_t` 레이아웃 + 선언 추가**

`string_t` 섹션 뒤에 추가:

```cpp
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
```

- [ ] **Step 4: `ingot.cpp` — create/destroy 구현**

`namespace ingot {` 내(익명 네임스페이스는 사용하지 않음 — 헤더 선언 함수는 일반 정의)에 추가. 내부 성장 헬퍼는 `ingot.cpp` 의 익명 네임스페이스에 둔다:

```cpp
namespace {

// 내부: 필요 용량 보장. 2배 성장, 최소 32바이트. resize 시도 후 alloc+copy+free.
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
```

- [ ] **Step 5: create/destroy 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: create/destroy/double-destroy/lazy 4개 케이스 통과.

- [ ] **Step 6: append + 성장 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("string_builder_t: append_char and growth") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 2);
    int64_t cap_before = ingot::sb_capacity(b);
    CHECK_MESSAGE(cap_before == 2, "initial capacity 2");

    ingot::sb_append_char(b, 'a');
    ingot::sb_append_char(b, 'b');
    CHECK_MESSAGE(ingot::sb_len(b) == 2, "len 2, still fits");
    CHECK_MESSAGE(ingot::sb_capacity(b) == 2, "no growth yet");

    ingot::sb_append_char(b, 'c');
    CHECK_MESSAGE(ingot::sb_len(b) == 3, "len 3 after overflow append");
    CHECK_MESSAGE(ingot::sb_capacity(b) >= 3, "capacity grew");
    CHECK_MESSAGE(ingot::sb_capacity(b) >= cap_before * 2, "at least doubled");

    ingot::sb_append_char(b, 'd');
    CHECK_MESSAGE(b.data[0] == 'a' && b.data[1] == 'b' && b.data[2] == 'c' && b.data[3] == 'd',
                  "bytes preserved across growth");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: append_cstr/bytes/view") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_append_cstr(b, "hello, ");
    ingot::sb_append_view(b, ingot::str_from_cstr("world"));
    ingot::sb_append_bytes(b, "!", 1);

    CHECK_MESSAGE(ingot::sb_len(b) == 13, "total len");
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("hello, world!")),
                  "assembled string");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: reserve") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 4);
    ingot::sb_reserve(b, 100);
    CHECK_MESSAGE(ingot::sb_capacity(b) >= 100, "capacity >= reserved");
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "len unchanged");
    ingot::sb_reserve(b, 50);
    CHECK_MESSAGE(ingot::sb_capacity(b) >= 100, "reserve does not shrink");
    ingot::sb_destroy(b);
}
```

- [ ] **Step 7: append 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`sb_append_*`, `sb_reserve`, `sb_to_string` 정의 없음).

- [ ] **Step 8: `ingot.h` — append/reserve/to_string 선언 추가**

`string_builder_t` 섹션에 추가:

```cpp
void sb_append_char(string_builder_t& b, char c);
void sb_append_bytes(string_builder_t& b, const char* p, int64_t n);
void sb_append_cstr(string_builder_t& b, const char* s);
void sb_append_view(string_builder_t& b, string_t v);
void sb_reserve(string_builder_t& b, int64_t total_capacity);

inline string_t sb_to_string(const string_builder_t& b) {
    return string_t{.data = b.data, .len = b.len};
}
```

- [ ] **Step 9: `ingot.cpp` — append/reserve 구현**

`namespace ingot {` 내 `sb_destroy` 뒤(익명 네임스페이스 `sb_ensure_capacity` 는 이미 위에 정의됨)에 추가:

```cpp
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
```

> 참고: `sb_ensure_capacity` 는 `ingot.cpp` 의 익명 네임스페이스에 있으므로 같은 TU 의 `sb_*` 정의에서 호출 가능. 헤더에는 노출 안 됨.

- [ ] **Step 10: append 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: append/growth/cstr-bytes-view/reserve 4개 케이스 통과.

- [ ] **Step 11: 변형 + at + mutable data 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("string_builder_t: clear/truncate/pop") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 16);
    ingot::sb_append_cstr(b, "hello");
    int64_t cap = ingot::sb_capacity(b);

    ingot::sb_truncate(b, 3);
    CHECK_MESSAGE(ingot::sb_len(b) == 3, "truncated to 3");
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("hel")),
                  "contents after truncate");

    ingot::sb_pop(b);
    CHECK_MESSAGE(ingot::sb_len(b) == 2, "pop removes one byte");

    ingot::sb_clear(b);
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "clear zeroes len");
    CHECK_MESSAGE(ingot::sb_capacity(b) == cap, "clear keeps capacity");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: at and mutable data") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 8);
    ingot::sb_append_cstr(b, "abc");
    CHECK_MESSAGE(ingot::sb_at(b, 1) == 'b', "at 1");
    ingot::sb_data(b)[0] = 'A';
    CHECK_MESSAGE(ingot::sb_at(b, 0) == 'A', "mutable data write");
    ingot::sb_destroy(b);
}
```

- [ ] **Step 12: 변형 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`sb_truncate`, `sb_pop`, `sb_clear`, `sb_at`, mutable `sb_data` 정의 없음).

- [ ] **Step 13: `ingot.h` — 변형/at/mutable data 추가**

`string_builder_t` 섹션에 추가:

```cpp
inline char* sb_data(string_builder_t& b) { return b.data; }

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
```

- [ ] **Step 14: 변형 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: clear/truncate/pop + at/mutable 2개 케이스 통과.

- [ ] **Step 15: to_cstring 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("string_builder_t: to_cstring") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_append_cstr(b, "hello");

    char* cstr = ingot::sb_to_cstring(b, heap);
    REQUIRE_MESSAGE(cstr != nullptr, "to_cstring should allocate");
    CHECK_MESSAGE(std::strcmp(cstr, "hello") == 0, "NUL-terminated copy");
    CHECK_MESSAGE(cstr[5] == '\0', "explicit NUL at len");
    heap.free(cstr, ingot::sb_len(b) + 1);
    ingot::sb_destroy(b);
}
```

(`tests/test_string.cpp` 상단에 `#include <cstring>` 가 필요하면 추가 — `std::strcmp` 용.)

- [ ] **Step 16: `ingot.h` + `ingot.cpp` — to_cstring 추가**

`ingot.h` 선언(`string_builder_t` 섹션):

```cpp
char* sb_to_cstring(const string_builder_t& b, allocator_t& alloc);
```

`ingot.cpp` 구현:

```cpp
char* sb_to_cstring(const string_builder_t& b, allocator_t& alloc) {
    char* out = static_cast<char*>(alloc.alloc(b.len + 1, 1));
    ingot_assert_(out != nullptr, "sb_to_cstring: allocation failed");
    if (b.len > 0) {
        std::memcpy(out, b.data, static_cast<size_t>(b.len));
    }
    out[b.len] = '\0';
    return out;
}
```

`tests/test_string.cpp` 최상단 include 강화:

```cpp
#include <cstring>

#include "doctest.h"

#include "ingot.h"
```

- [ ] **Step 17: to_cstring 테스트 통과 확인 + 아레나 할당자 회귀**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: to_cstring 케이스 통과. 모든 string_builder_t 케이스 통과.

- [ ] **Step 18: 커밋**

```bash
git add ingot.h ingot.cpp tests/test_string.cpp
git commit -m "Add string_builder_t heap owner with growth and full lifecycle

Growable char buffer backed by allocator_t*. 2x geometric growth with
32-byte minimum, lazy allocation on capacity=0, in-place resize attempt
before alloc+copy+free. Append variants (char/bytes/cstr/view), reserve,
clear/truncate/pop, const and mutable data access, O(1) to_string view,
allocating to_cstring for C interop. No implicit NUL (Option 2)."
```

---

## Task 5: `static_string_builder_t<N>` — 스택 소유자 (하드 캡)

인라인 버퍼, 컴파일 타임 고정 용량 N, 오버플로우 시 `ingot_assert_`. 할당자 없음. 모든 함수는 `template <int64_t N>` 이므로 헤더에 인라인.

**Files:**
- Modify: `ingot.h`
- Modify: `tests/test_string.cpp`

- [ ] **Step 1: create + append + 하드 캡 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("static_string_builder_t: create and append") {
    ingot::static_string_builder_t<16> b;
    ingot::ssb_create(b);
    CHECK_MESSAGE(ingot::ssb_len(b) == 0, "len 0 after create");
    CHECK_MESSAGE(ingot::ssb_is_empty(b), "empty");

    ingot::ssb_append_cstr(b, "hello");
    CHECK_MESSAGE(ingot::ssb_len(b) == 5, "len 5");
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b), ingot::str_from_cstr("hello")),
                  "contents");
    CHECK_MESSAGE(ingot::ssb_capacity(b) == 16, "capacity is N");
}

TEST_CASE("static_string_builder_t: is_full and remaining") {
    ingot::static_string_builder_t<3> b;
    ingot::ssb_create(b);
    CHECK_MESSAGE(!ingot::ssb_is_full(b), "not full initially");
    ingot::ssb_append_char(b, 'a');
    ingot::ssb_append_char(b, 'b');
    ingot::ssb_append_char(b, 'c');
    CHECK_MESSAGE(ingot::ssb_is_full(b), "full at N");
    CHECK_MESSAGE(ingot::ssb_len(b) == 3, "len == N");
}

TEST_CASE("static_string_builder_t: append_view/bytes") {
    ingot::static_string_builder_t<32> b;
    ingot::ssb_create(b);
    ingot::ssb_append_view(b, ingot::str_from_cstr("foo"));
    ingot::ssb_append_bytes(b, "bar", 3);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b), ingot::str_from_cstr("foobar")),
                  "assembled");
}
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`static_string_builder_t`, `ssb_*` 정의 없음).

- [ ] **Step 3: `ingot.h` — 레이아웃 + create + append 추가**

`string_builder_t` 섹션 뒤에 추가:

```cpp
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
inline void ssb_append_char(static_string_builder_t<N>& b, char c) {
    ingot_assert_(b.len < N,
                  "ssb_append_char: overflow (len=%lld, capacity=%lld)",
                  static_cast<long long>(b.len), static_cast<long long>(N));
    b.buffer[b.len] = c;
    b.len++;
}

template <int64_t N>
inline void ssb_append_cstr(static_string_builder_t<N>& b, const char* s) {
    ingot_assert_(s != nullptr, "ssb_append_cstr: null pointer");
    ssb_append_bytes(b, s, static_cast<int64_t>(std::strlen(s)));
}

template <int64_t N>
inline void ssb_append_view(static_string_builder_t<N>& b, string_t v) {
    ssb_append_bytes(b, v.data, v.len);
}
```

- [ ] **Step 4: create/append/is_full 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 신규 3개 케이스 통과.

- [ ] **Step 5: 변형 + at + to_cstring 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("static_string_builder_t: clear/truncate/pop/at") {
    ingot::static_string_builder_t<16> b;
    ingot::ssb_create(b);
    ingot::ssb_append_cstr(b, "hello");

    CHECK_MESSAGE(ingot::ssb_at(b, 0) == 'h', "at 0");
    CHECK_MESSAGE(ingot::ssb_at(b, 4) == 'o', "at 4");

    ingot::ssb_truncate(b, 3);
    CHECK_MESSAGE(ingot::ssb_len(b) == 3, "truncate to 3");

    ingot::ssb_pop(b);
    CHECK_MESSAGE(ingot::ssb_len(b) == 2, "pop");

    ingot::ssb_clear(b);
    CHECK_MESSAGE(ingot::ssb_len(b) == 0, "clear");
    CHECK_MESSAGE(ingot::ssb_capacity(b) == 16, "capacity unchanged");
}

TEST_CASE("static_string_builder_t: to_cstring") {
    ingot::static_string_builder_t<16> b;
    ingot::ssb_create(b);
    ingot::ssb_append_cstr(b, "hi");

    ingot::heap_allocator_t heap;
    char* cstr = ingot::ssb_to_cstring(b, heap);
    REQUIRE_MESSAGE(cstr != nullptr, "allocated");
    CHECK_MESSAGE(std::strcmp(cstr, "hi") == 0, "NUL-terminated copy");
    CHECK_MESSAGE(cstr[2] == '\0', "NUL at len");
    heap.free(cstr, ingot::ssb_len(b) + 1);
}
```

- [ ] **Step 6: 변형 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`ssb_truncate`, `ssb_pop`, `ssb_clear`, `ssb_at`, `ssb_to_cstring` 정의 없음).

- [ ] **Step 7: `ingot.h` — 변형/at/to_cstring 추가**

`static_string_builder_t` 섹션에 추가:

```cpp
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
```

> 참고: `ssb_to_cstring` 은 템플릿이므로 `ingot.cpp` 가 아닌 헤더에 인라인으로 정의해야 한다.
```

- [ ] **Step 8: 변형 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 신규 2개 케이스 통과. 모든 static_string_builder_t 케이스 통과.

- [ ] **Step 9: 커밋**

```bash
git add ingot.h tests/test_string.cpp
git commit -m "Add static_string_builder_t<N> stack owner with hard-cap

Inline fixed-capacity buffer (compile-time N), no allocator, asserts on
overflow (mirrors static_vector_t contract). Template free functions
ssb_* with append/clear/truncate/pop, capacity/len/is_empty/is_full
inspection, const+mutable data, at, O(1) to_string view, allocating
ssb_to_cstring with explicit allocator."
```

---

## Task 6: UTF-8 기본 함수 (검증, 디코드, 인코드, 카운트)

관대한 디코드 + 엄격한 검증 분리. rune 타입 `char32_t`.

**Files:**
- Modify: `ingot.h`
- Modify: `ingot.cpp`
- Modify: `tests/test_string.cpp`

- [ ] **Step 1: 디코드/인코드 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("utf8: decode ASCII") {
    const char* s = "A";
    int width = 0;
    char32_t r = ingot::utf8_decode_rune(s, 1, &width);
    CHECK_MESSAGE(r == U'A', "ASCII codepoint");
    CHECK_MESSAGE(width == 1, "ASCII width 1");
}

TEST_CASE("utf8: decode multibyte") {
    const char* s = "\xEC\x84\xB8";  // 세 (U+C138)
    int width = 0;
    char32_t r = ingot::utf8_decode_rune(s, 3, &width);
    CHECK_MESSAGE(r == 0xC138, "Korean syllable 세");
    CHECK_MESSAGE(width == 3, "3-byte width");
}

TEST_CASE("utf8: decode invalid byte") {
    const char* s = "\xFF";
    int width = 0;
    char32_t r = ingot::utf8_decode_rune(s, 1, &width);
    CHECK_MESSAGE(r == ingot::utf8_rune_error, "invalid -> U+FFFD");
    CHECK_MESSAGE(width == 1, "advance 1 byte");
}

TEST_CASE("utf8: encode roundtrip") {
    char32_t runes[] = {U'A', 0xC138, U'\U0001F600'};  // A, 세, 😀
    for (char32_t r : runes) {
        char buf[4] = {0, 0, 0, 0};
        int width = ingot::utf8_encode_rune(r, buf);
        CHECK_MESSAGE(width > 0, "valid rune encodes");
        int dwidth = 0;
        char32_t back = ingot::utf8_decode_rune(buf, 4, &dwidth);
        CHECK_MESSAGE(back == r, "roundtrip preserves codepoint");
        CHECK_MESSAGE(dwidth == width, "roundtrip preserves width");
    }
}

TEST_CASE("utf8: encode invalid rune returns 0") {
    char buf[4] = {0, 0, 0, 0};
    int width = ingot::utf8_encode_rune(0x110000, buf);  // out of range
    CHECK_MESSAGE(width == 0, "out-of-range rejected");
    width = ingot::utf8_encode_rune(0xD800, buf);  // surrogate
    CHECK_MESSAGE(width == 0, "surrogate rejected");
}
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`utf8_decode_rune`, `utf8_encode_rune`, `utf8_rune_error` 정의 없음).

- [ ] **Step 3: `ingot.h` — UTF-8 섹션 추가**

`static_string_builder_t` 섹션 뒤(파일 끝의 `} // namespace ingot` 직전)에 추가:

```cpp
// === UTF-8 헬퍼 (옵션) ===

inline constexpr char32_t utf8_rune_error = 0xFFFD;

char32_t utf8_decode_rune(const char* p, int64_t remaining, int* out_width);
int     utf8_encode_rune(char32_t rune, char* out_buf);
bool    utf8_validate(string_t s);
int64_t utf8_rune_count(string_t s);
```

- [ ] **Step 4: `ingot.cpp` — decode/encode 구현**

`namespace ingot {` 내에 추가:

```cpp
char32_t utf8_decode_rune(const char* p, int64_t remaining, int* out_width) {
    ingot_assert_(remaining > 0, "utf8_decode_rune: remaining <= 0");
    ingot_assert_(out_width != nullptr, "utf8_decode_rune: null out_width");
    unsigned char b0 = static_cast<unsigned char>(p[0]);

    if (b0 < 0x80) {
        *out_width = 1;
        return static_cast<char32_t>(b0);
    }
    if ((b0 & 0xC0) == 0x80) {
        *out_width = 1;
        return utf8_rune_error;
    }

    int      width;
    char32_t code;
    char32_t min;
    if ((b0 & 0xE0) == 0xC0) {
        width = 2; code = b0 & 0x1F; min = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        width = 3; code = b0 & 0x0F; min = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        width = 4; code = b0 & 0x07; min = 0x10000;
    } else {
        *out_width = 1;
        return utf8_rune_error;
    }

    if (remaining < width) {
        *out_width = 1;
        return utf8_rune_error;
    }
    for (int i = 1; i < width; ++i) {
        unsigned char bi = static_cast<unsigned char>(p[i]);
        if ((bi & 0xC0) != 0x80) {
            *out_width = 1;
            return utf8_rune_error;
        }
        code = (code << 6) | static_cast<char32_t>(bi & 0x3F);
    }
    if (code < min || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        *out_width = 1;
        return utf8_rune_error;
    }
    *out_width = width;
    return code;
}

int utf8_encode_rune(char32_t rune, char* out_buf) {
    ingot_assert_(out_buf != nullptr, "utf8_encode_rune: null out_buf");
    if (rune > 0x10FFFF || (rune >= 0xD800 && rune <= 0xDFFF)) {
        return 0;
    }
    if (rune < 0x80) {
        out_buf[0] = static_cast<char>(rune);
        return 1;
    }
    if (rune < 0x800) {
        out_buf[0] = static_cast<char>(0xC0 | (rune >> 6));
        out_buf[1] = static_cast<char>(0x80 | (rune & 0x3F));
        return 2;
    }
    if (rune < 0x10000) {
        out_buf[0] = static_cast<char>(0xE0 | (rune >> 12));
        out_buf[1] = static_cast<char>(0x80 | ((rune >> 6) & 0x3F));
        out_buf[2] = static_cast<char>(0x80 | (rune & 0x3F));
        return 3;
    }
    out_buf[0] = static_cast<char>(0xF0 | (rune >> 18));
    out_buf[1] = static_cast<char>(0x80 | ((rune >> 12) & 0x3F));
    out_buf[2] = static_cast<char>(0x80 | ((rune >> 6) & 0x3F));
    out_buf[3] = static_cast<char>(0x80 | (rune & 0x3F));
    return 4;
}
```

- [ ] **Step 5: decode/encode 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: decode ASCII/multibyte/invalid + encode roundtrip/invalid 5개 케이스 통과.

- [ ] **Step 6: validate + rune_count 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("utf8: validate") {
    CHECK_MESSAGE(ingot::utf8_validate(ingot::str_from_cstr("hello")), "ASCII valid");
    CHECK_MESSAGE(ingot::utf8_validate(ingot::str_from_cstr("Hello, 세계")), "multibyte valid");
    CHECK_MESSAGE(ingot::utf8_validate(ingot::str_from("", 0)), "empty valid");

    const char* bad = "\xFF\xFE";
    CHECK_MESSAGE(!ingot::utf8_validate(ingot::str_from(bad, 2)), "invalid bytes rejected");

    const char* truncated = "\xEC\x84";  // 3바이트 시퀀스인데 2바이트만
    CHECK_MESSAGE(!ingot::utf8_validate(ingot::str_from(truncated, 2)), "truncated rejected");
}

TEST_CASE("utf8: rune_count") {
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from_cstr("abc")) == 3, "ASCII 3 runes");
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from_cstr("세계")) == 2, "2 Korean runes");
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from_cstr("Hello, 세계")) == 9,
                  "mixed ASCII + multibyte");
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from("", 0)) == 0, "empty 0 runes");
}
```

- [ ] **Step 7: validate/rune_count 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패는 아님(헤더에 선언됨) — 링크 실패 (`utf8_validate`, `utf8_rune_count` 구현 없음).

- [ ] **Step 8: `ingot.cpp` — validate + rune_count 구현**

`namespace ingot {` 내 `utf8_encode_rune` 뒤에 추가:

```cpp
bool utf8_validate(string_t s) {
    const char* p = s.data;
    int64_t     remaining = s.len;
    while (remaining > 0) {
        int      width;
        char32_t r = utf8_decode_rune(p, remaining, &width);
        if (r == utf8_rune_error && width == 1) {
            return false;
        }
        p += width;
        remaining -= width;
    }
    return true;
}

int64_t utf8_rune_count(string_t s) {
    const char* p = s.data;
    int64_t     remaining = s.len;
    int64_t     count = 0;
    while (remaining > 0) {
        int width;
        utf8_decode_rune(p, remaining, &width);
        p += width;
        remaining -= width;
        count++;
    }
    return count;
}
```

- [ ] **Step 9: validate/rune_count 테스트 통과 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: validate + rune_count 2개 케이스 통과. 모든 UTF-8 기본 케이스 통과.

- [ ] **Step 10: 커밋**

```bash
git add ingot.h ingot.cpp tests/test_string.cpp
git commit -m "Add UTF-8 helpers: validate, decode, encode, rune_count

Lenient decode (returns U+FFFD on invalid, advances 1 byte — iteration
safe), strict validate (whole-string bool), encode (1-4 bytes, 0 on
invalid), lenient rune_count. rune type is char32_t. opt-in module,
byte-oriented by default."
```

---

## Task 7: UTF-8 rune 반복 뷰 (`utf8_rune_view_t`)

`string_t` 를 `char32_t` rune 시퀀스로 range-for 순회. 캐시된 커서로 rune 당 1회 디코드. operator 들은 자유 함수.

**Files:**
- Modify: `ingot.h`
- Modify: `tests/test_string.cpp`

- [ ] **Step 1: range-for 뷰 실패 테스트 작성**

`tests/test_string.cpp` 에 추가:

```cpp
TEST_CASE("utf8_rune_view: range-for over multibyte") {
    ingot::string_t s = ingot::str_from_cstr("A세");  // A(1) + 세(3) = 4 bytes, 2 runes
    char32_t collected[4];
    int count = 0;
    for (char32_t r : ingot::utf8_runes(s)) {
        collected[count++] = r;
    }
    CHECK_MESSAGE(count == 2, "2 runes from 4 bytes");
    CHECK_MESSAGE(collected[0] == U'A', "first rune A");
    CHECK_MESSAGE(collected[1] == 0xC138, "second rune 세");
}

TEST_CASE("utf8_rune_view: empty string") {
    ingot::string_t e = ingot::str_from("", 0);
    int count = 0;
    for (char32_t r : ingot::utf8_runes(e)) {
        (void)r;
        count++;
    }
    CHECK_MESSAGE(count == 0, "no runes from empty");
}

TEST_CASE("utf8_rune_view: invalid byte yields replacement") {
    const char* bad = "\xFF\x41";  // invalid + 'A'
    ingot::string_t s = ingot::str_from(bad, 2);
    int count = 0;
    char32_t first = 0;
    for (char32_t r : ingot::utf8_runes(s)) {
        if (count == 0) first = r;
        count++;
    }
    CHECK_MESSAGE(count == 2, "2 runes (1 invalid + 1 valid)");
    CHECK_MESSAGE(first == ingot::utf8_rune_error, "invalid byte -> U+FFFD");
}
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: 컴파일 실패 (`utf8_rune_view_t`, `utf8_runes`, 관련 operator 들 정의 없음).

- [ ] **Step 3: `ingot.h` — rune 뷰 타입 + operator 추가**

UTF-8 섹션 끝(`utf8_rune_count` 선언 뒤)에 추가:

```cpp
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
```

- [ ] **Step 4: 뷰 테스트 통과 확인 + 전체 회귀**

```bash
cmake --build build && ./build/tests/test_string
```

Expected: rune 뷰 3개 케이스 통과. 전체 `test_string` + `test_static_vector` 통과.

- [ ] **Step 5: 전체 빌드 + ctest 최종 확인**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `test_static_vector` + `test_string` 모두 통과.

- [ ] **Step 6: 커밋**

```bash
git add ingot.h tests/test_string.cpp
git commit -m "Add utf8_rune_view_t for range-for rune iteration

Non-owning view presenting string_t as a char32_t rune sequence.
Cached cursor decodes once per rune in begin/++. Free-function operators
(operator*, operator++, operator!=) for range-for support. POD types
static_asserted. Lenient — invalid bytes yield utf8_rune_error."
```

---

## 완료 기준 (Definition of Done)

- [ ] `string_t`, `string_builder_t`, `static_string_builder_t<N>` 세 타입 모두 구현 + POD `static_assert`.
- [ ] UTF-8 헬퍼(`utf8_validate`/`decode_rune`/`encode_rune`/`rune_count`) + `utf8_rune_view_t` 구현.
- [ ] `tests/test_string.cpp` 의 모든 `TEST_CASE` 통과.
- [ ] `tests/test_static_vector.cpp` 회귀 없음.
- [ ] `ctest --test-dir build --output-on-failure` 전체 녹색.
- [ ] 코딩 표준 준수: 자유 함수 + 접두사(`str_`/`sb_`/`ssb_`/`utf8_`), `snake_case_t`, `ingot_assert_`, 한국어 주석, 영어 식별자/로그.
- [ ] 각 태스크별 커밋 (영어 커밋 메시지).

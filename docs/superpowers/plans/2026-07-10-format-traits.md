# 포매터 트레이트 및 컨테이너 포매터 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 런타임 함수 포인터 기반 `format_dispatch<T>`를 제거하고 `format_traits<T>` 컴파일 타임 트레이트를 도입하여 `sb_format`과 `ssb_format` 모두에서 사용자 정의 타입 포매팅을 지원한다. 6종 컨테이너 타입에 빌트인 포매터를 제공한다.

**Architecture:** `format_traits<T>` 템플릿 특수화로 타입별 포매터를 정의하고, `format_write` 오버로드 2종이 각각 `sb_format`/`ssb_format`으로 포워딩한다. `format_register_` 매크로는 `format_traits` 특수화를 생성하며 네임스페이스 스코프에서만 사용 가능하다. `detail::format_one_sb`와 `detail::format_one_ssb`의 `else` 분기에서 `format_traits<T>::write(b, val)`를 호출한다.

**Tech Stack:** C++23, doctest, CMake

---

### File Structure

| 파일 | 역할 |
|---|---|
| `ingot.h:552-562` | `format_traits<T>` 기본 템플릿 + `format_write` 전방 선언 추가, 기존 `format_fn_t`/`format_dispatch`/`format_register` 제거 |
| `ingot.h:640-690` (`detail` 네임스페이스) | `format_one_sb`와 `format_one_ssb`의 `else` 분기 수정 |
| `ingot.h:693-706` | `format_register_` 매크로 재작성 (trait 특수화 생성) |
| `ingot.h:839+` (파일 끝, 네임스페이스 닫힘 전) | `format_write` 구현 + 빌트인 컨테이너 `format_traits` 특수화 |
| `tests/test_string.cpp:1-8` | 파일 스코프 테스트 타입 정의 및 `format_register_` 호출 |
| `tests/test_string.cpp:746-820` | 기존 런타임 API 테스트를 trait 방식으로 마이그레이션, 새 테스트 추가 |

---

### Task 1: `format_traits<T>` 기본 템플릿과 `format_write` 전방 선언 추가, 기존 런타임 API 제거

**Files:**
- Modify: `ingot.h:552-562`

- [ ] **Step 1: `ingot.h:552-562` 대체**

`// === 스트링 포맷 ===` 주석 아래 영역을 다음으로 교체:

```cpp
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

template <typename... Args>
void format_write(string_builder_t& b, string_t fmt, Args&&... args);

template <int64_t N, typename... Args>
void format_write(static_string_builder_t<N>& b, string_t fmt, Args&&... args);
```

삭제되는 기존 코드:
```cpp
using format_fn_t = void (*)(string_builder_t& b, const void* ptr);
template <typename T> inline format_fn_t format_dispatch = nullptr;
template <typename T> void format_register(format_fn_t fn) { format_dispatch<T> = fn; }
```

- [ ] **Step 2: Debug 빌드**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DINGOT_BUILD_TESTS=ON
cmake --build build
```

- [ ] **Step 3: Commit**

```bash
git add ingot.h
git commit -m "feat: add format_traits primary template, remove runtime dispatch API"
```

---

### Task 2: `format_one_sb`와 `format_one_ssb`의 `else` 분기 수정

**Files:**
- Modify: `ingot.h` (detail 네임스페이스 내 `format_one_sb`, `format_one_ssb`)

- [ ] **Step 1: `format_one_sb` 수정**

`detail::format_one_sb<T>` (약 646-651행)의 `else` 분기:

기존:
```cpp
    } else {
        if (format_dispatch<T> != nullptr) {
            format_dispatch<T>(b, ptr);
        } else {
            ingot_assert_(false, "sb_format: unsupported type (no formatter registered)");
        }
    }
```

변경:
```cpp
    } else {
        format_traits<T>::write(b, val);
    }
```

- [ ] **Step 2: `format_one_ssb` 수정**

`detail::format_one_ssb<N, T>` (약 687행)의 `else` 분기:

기존:
```cpp
    } else {
        static_assert(sizeof(T) == 0, "ssb_format: unsupported type");
    }
```

변경:
```cpp
    } else {
        format_traits<T>::write(b, val);
    }
```

- [ ] **Step 3: Commit**

```bash
git add ingot.h
git commit -m "feat: route format_one_sb/ssb else-branch through format_traits"
```

---

### Task 3: `format_register_` 매크로 재작성

**Files:**
- Modify: `ingot.h` (약 693-706행)

- [ ] **Step 1: 매크로 대체**

기존:
```cpp
#define format_register_(Type, fmt_str, ...)                              \
    namespace {                                                            \
        static const auto _fmt_reg_##Type =                                \
            (::ingot::format_register<Type>(                               \
                [](::ingot::string_builder_t& b,                           \
                   const void* _ptr) {                                     \
                    const auto& _v =                                       \
                        *static_cast<const Type*>(_ptr);                    \
                    ::ingot::sb_format(b, ::ingot::str_lit(fmt_str),      \
                                        __VA_ARGS__);                      \
                }), 0);                                                    \
    }
```

변경:
```cpp
#define format_register_(Type, fmt_str, ...)                              \
    template <>                                                            \
    struct ::ingot::format_traits<Type> {                                  \
        template <typename _B>                                             \
        static void write(_B& b, const Type& _v) {                        \
            format_write(b, ::ingot::str_lit(fmt_str),                    \
                         __VA_ARGS__);                                     \
        }                                                                  \
    };
```

- [ ] **Step 2: Commit**

```bash
git add ingot.h
git commit -m "feat: rewrite format_register_ macro to specialize format_traits"
```

---

### Task 4: `format_write` 구현 추가

**Files:**
- Modify: `ingot.h` (파일 끝, `ssb_format` 정의 이후 `} // namespace ingot` 직전)

- [ ] **Step 1: `ingot.h`에 `format_write` 오버로드 추가**

`ssb_format` 템플릿 함수 닫는 `}` 이후, `} // namespace ingot` 이전에 추가:

```cpp
template <typename... Args>
void format_write(string_builder_t& b, string_t fmt, Args&&... args) {
    sb_format(b, fmt, static_cast<Args&&>(args)...);
}

template <int64_t N, typename... Args>
void format_write(static_string_builder_t<N>& b, string_t fmt, Args&&... args) {
    ssb_format(b, fmt, static_cast<Args&&>(args)...);
}
```

- [ ] **Step 2: Build and run existing tests to verify no regression**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 기존 `format_register_`를 사용하는 `test_vec3` 테스트가 통과해야 함. `test_string.cpp:8`의 `format_register_(test_vec3, ...)`가 새 매크로로 trait 특수화를 생성하고, `sb_format`과 `ssb_format` 모두에서 동작한다.

- [ ] **Step 3: Commit**

```bash
git add ingot.h
git commit -m "feat: add format_write overloads for sb/ssb forwarding"
```

---

### Task 5: 빌트인 컨테이너 `format_traits` 특수화 추가

**Files:**
- Modify: `ingot.h` (파일 끝, `format_write` 정의 이후)

- [ ] **Step 1: `static_vector_t<T>` 포매터 추가**

```cpp
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
```

- [ ] **Step 2: `view_t<T>` 포매터 추가**

```cpp
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
```

- [ ] **Step 3: `string_t` 포매터 추가**

```cpp
template <>
struct format_traits<string_t> {
    template <typename B>
    static void write(B& b, string_t val) {
        format_write(b, str_lit("{}"), val);
    }
};
```

- [ ] **Step 4: `string_builder_t` 포매터 추가**

```cpp
template <>
struct format_traits<string_builder_t> {
    template <typename B>
    static void write(B& b, const string_builder_t& sb) {
        format_write(b, str_lit("{}"), sb_to_string(sb));
        format_write(b, str_lit(" [{}/{}]"), sb_len(sb), sb_capacity(sb));
    }
};
```

- [ ] **Step 5: `static_string_builder_t<N>` 포매터 추가**

```cpp
template <int64_t N>
struct format_traits<static_string_builder_t<N>> {
    template <typename B>
    static void write(B& b, const static_string_builder_t<N>& sb) {
        format_write(b, str_lit("{}"), ssb_to_string(sb));
        format_write(b, str_lit(" [{}/{}]"), ssb_len(sb), N);
    }
};
```

- [ ] **Step 6: `utf8_rune_view_t` 포매터 추가**

```cpp
template <>
struct format_traits<utf8_rune_view_t> {
    template <typename B>
    static void write(B& b, utf8_rune_view_t v) {
        for (auto c = begin(v); c != end(v); ++c) {
            format_write(b, str_lit("{}"), *c);
        }
    }
};
```

- [ ] **Step 7: Build**

```bash
cmake --build build
```

- [ ] **Step 8: Commit**

```bash
git add ingot.h
git commit -m "feat: add builtin format_traits specializations for container types"
```

---

### Task 6: 테스트 마이그레이션 — 기존 런타임 API 테스트 제거 및 trait 방식으로 대체

**Files:**
- Modify: `tests/test_string.cpp`

- [ ] **Step 1: 파일 스코프에 새 테스트 타입 정의 추가**

`test_string.cpp`의 `test_vec3` 정의(7-8행) 바로 아래에 추가:

```cpp
struct test_point2 { int x, y; };
format_register_(test_point2, "[{}, {}]", _v.x, _v.y)

struct test_tagged { int id; const char* name; };
format_register_(test_tagged, "[{}, {}]", _v.id, ingot::str_from_cstr(_v.name))

struct test_ssb_val { int a, b; };
format_register_(test_ssb_val, "({}, {})", _v.a, _v.b)
```

- [ ] **Step 2: 기존 `format_register` 직접 호출 테스트를 `format_register_` 기반으로 대체**

`sb_format: user-defined type via format_register` (약 759행)와 `sb_format: overwrite registered formatter` (약 779행) 테스트 케이스 2개를 제거하고 다음 2개로 대체:

```cpp
TEST_CASE("sb_format: local struct via macro") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("p: {}"), test_point2{10, 20});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("p: [10, 20]")),
                  "point2 formatted");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: user-defined type with name field") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("t: {}"), test_tagged{1, "a"});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("t: [1, a]")),
                  "tagged formatted");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 3: `ssb_format` 커스텀 타입 테스트 추가**

`ssb_format: append to non-empty` 테스트(약 735-744행) 바로 다음에 추가:

```cpp
TEST_CASE("ssb_format: user-defined type") {
    ingot::static_string_builder_t<128> b;
    ingot::ssb_create(b);

    ingot::ssb_format(b, ingot::str_lit("val: {}"), test_ssb_val{3, 7});
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("val: (3, 7)")),
                  "ssb user-defined type");
}
```

- [ ] **Step 4: `sb_format: user-defined type via macro` (test_vec3) 테스트 이름 변경**

7번째 `TEST_CASE` 인스턴스의 이름을 `"sb_format: user-defined struct via macro"`로 변경하여 중복 방지.

- [ ] **Step 5: Commit**

```bash
git add tests/test_string.cpp
git commit -m "test: migrate runtime registration tests to format_traits"
```

---

### Task 7: 컨테이너 포매터 및 재귀 포매팅 테스트 추가

**Files:**
- Modify: `tests/test_string.cpp` (파일 끝 `sb_format: mixed builtin and user-defined types` 테스트 이후)

- [ ] **Step 1: 빈 컨테이너 포매팅 테스트 추가**

```cpp
TEST_CASE("format container: empty static_vector") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;
    ingot::sv_create(v, heap, 4);

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: []")),
                  "empty vector");

    ingot::sb_destroy(b);
    ingot::sv_destroy(v);
}
```

- [ ] **Step 2: 요소가 있는 static_vector 포매팅 테스트 추가**

```cpp
TEST_CASE("format container: static_vector with elements") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;
    ingot::sv_create(v, heap, 4);
    ingot::sv_push(v, 1);
    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: [1, 2, 3]")),
                  "vector with elements");

    ingot::sb_destroy(b);
    ingot::sv_destroy(v);
}
```

- [ ] **Step 3: 재귀 포매팅 (중첩 컨테이너) 테스트 추가**

`test_vec3` 타입을 사용한 벡터 포매팅 (재귀적으로 `test_vec3` 포매터가 호출됨):

```cpp
TEST_CASE("format container: recursive formatting") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<test_vec3> v;
    ingot::sv_create(v, heap, 4);
    ingot::sv_push(v, test_vec3{1, 2, 3});
    ingot::sv_push(v, test_vec3{4, 5, 6});

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: [(1, 2, 3), (4, 5, 6)]")),
                  "recursive vector");

    ingot::sb_destroy(b);
    ingot::sv_destroy(v);
}
```

- [ ] **Step 4: string_t 포매팅 테스트 추가**

```cpp
TEST_CASE("format container: string_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::string_t s = ingot::str_from_cstr("hello");
    ingot::sb_format(b, ingot::str_lit("s: {}"), s);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("s: hello")),
                  "string_t");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 5: string_builder_t 포매팅 테스트 추가**

```cpp
TEST_CASE("format container: string_builder_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t sb;
    ingot::sb_create(sb, heap, 64);
    ingot::sb_append(sb, ingot::str_from_cstr("hi"));

    ingot::string_builder_t out;
    ingot::sb_create(out, heap, 0);
    ingot::sb_format(out, ingot::str_lit("b: {}"), sb);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(out),
                                   ingot::str_from_cstr("b: hi [2/64]")),
                  "string_builder_t");

    ingot::sb_destroy(out);
    ingot::sb_destroy(sb);
}
```

- [ ] **Step 6: static_string_builder_t 포매팅 테스트 추가**

```cpp
TEST_CASE("format container: static_string_builder_t") {
    ingot::heap_allocator_t heap;
    ingot::static_string_builder_t<32> ssb;
    ingot::ssb_create(ssb);
    ingot::ssb_append(ssb, ingot::str_from_cstr("abc"));

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("b: {}"), ssb);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("b: abc [3/32]")),
                  "static_string_builder_t");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 7: utf8_rune_view_t 포매팅 테스트 추가**

```cpp
TEST_CASE("format container: utf8_rune_view_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::utf8_rune_view_t r = ingot::utf8_runes(ingot::str_from_cstr("한글"));
    ingot::sb_format(b, ingot::str_lit("r: {}"), r);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("r: 한글")),
                  "utf8_rune_view_t");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 8: view_t 포매팅 테스트 추가**

```cpp
TEST_CASE("format container: view_t") {
    ingot::heap_allocator_t heap;
    int arr[] = {10, 20, 30};
    ingot::view_t<int> v = ingot::view_from(arr);

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: [10, 20, 30]")),
                  "view_t");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 9: Commit**

```bash
git add tests/test_string.cpp
git commit -m "test: add container formatter and recursive formatting tests"
```

---

### Task 8: 전체 빌드 및 테스트 검증

**Files:** (변경 없음)

- [ ] **Step 1: 전체 빌드**

```bash
cmake --build build
```

Expected: Build succeeds with no errors or warnings.

- [ ] **Step 2: 전체 테스트 실행**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 모든 테스트 통과.

- [ ] **Step 3: 상세 테스트 출력 확인**

```bash
./build/tests/test_string --success
```

Expected: 모든 테스트 케이스 SUCCESS, assertion 통과.


# 포매팅 시스템 사용자 정의 타입 확장 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `sb_format` 에 함수 포인터 기반 동적 포매터 등록 API 를 추가하여 사용자 정의 타입을 지원한다.

**Architecture:** 템플릿 정적 변수 `format_dispatch<T>` 에 타입별 함수 포인터를 저장한다. `format_one_sb` 의 `else` 분기에서 이 포인터를 확인하여, 등록된 포매터를 런타임에 호출한다. `format_register_` 매크로는 전역 정적 변수 초기화로 `main()` 이전에 자동 등록한다.

**Tech Stack:** C++23, doctest, CMake

---

### Task 1: `format_fn_t`, `format_dispatch<T>`, `format_register<T>` 선언 추가

**Files:**
- Modify: `ingot.h`

`detail` 네임스페이스 닫힌 직후(677행), `sb_format` 템플릿 함수 이전(679행)에 추가한다.

- [ ] **Step 1: `ingot.h:678` 에 추가**

```cpp
using format_fn_t = void (*)(string_builder_t& b, const void* ptr);

template <typename T>
inline format_fn_t format_dispatch = nullptr;

template <typename T>
void format_register(format_fn_t fn) {
    format_dispatch<T> = fn;
}
```

---

### Task 2: `format_one_sb` 의 `else` 분기를 런타임 디스패치로 수정

**Files:**
- Modify: `ingot.h:636-638`

- [ ] **Step 1: `format_one_sb` 의 `else` 분기 수정**

기존:
```cpp
    } else {
        static_assert(sizeof(T) == 0, "sb_format: unsupported type");
    }
```

변경:
```cpp
    } else {
        if (format_dispatch<T> != nullptr) {
            format_dispatch<T>(b, ptr);
        } else {
            ingot_assert_(false, "sb_format: unsupported type (no formatter registered)");
        }
    }
```

---

### Task 3: `format_register_` 헬퍼 매크로 추가

**Files:**
- Modify: `ingot.h`

`format_register<T>` 함수 정의 바로 아래에 추가한다.

- [ ] **Step 1: `format_register_` 매크로 추가**

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

---

### Task 4: 사용자 정의 타입 포매팅 테스트 추가

**Files:**
- Modify: `tests/test_string.cpp` (파일 끝 `ssb_format: append to non-empty` 테스트 다음, 741행 이후)

- [ ] **Step 1: 테스트 struct 정의 및 매크로 등록**

테스트 파일 상단 include 직후에 struct 정의와 매크로 등록을 추가:

```cpp
struct test_vec3 { float x, y, z; };
format_register_(test_vec3, "({}, {}, {})", _v.x, _v.y, _v.z)
```

- [ ] **Step 2: `format_register_` 매크로 등록 테스트 추가**

```cpp
TEST_CASE("sb_format: user-defined type via macro") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("vec: {}"), test_vec3{1.5f, 2.5f, 3.5f});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("vec: (1.5, 2.5, 3.5)")),
                  "vec3 formatted");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 3: `format_register` 직접 API 테스트 추가**

```cpp
TEST_CASE("sb_format: user-defined type via format_register") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    struct point2 { int x, y; };

    ingot::format_register<point2>([](ingot::string_builder_t& b, const void* ptr) {
        const auto& p = *static_cast<const point2*>(ptr);
        ingot::sb_format(b, ingot::str_lit("[{}, {}]"), p.x, p.y);
    });

    ingot::sb_format(b, ingot::str_lit("p: {}"), point2{10, 20});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("p: [10, 20]")),
                  "point2 formatted");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 4: 덮어쓰기 등록 테스트 추가**

```cpp
TEST_CASE("sb_format: overwrite registered formatter") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    struct tagged { int id; const char* name; };

    ingot::format_register<tagged>([](ingot::string_builder_t& b, const void* ptr) {
        const auto& t = *static_cast<const tagged*>(ptr);
        ingot::sb_format(b, ingot::str_lit("<{}>"), t.id);
    });
    ingot::sb_format(b, ingot::str_lit("t: {}"), tagged{1, "a"});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("t: <1>")),
                  "first registration");

    ingot::sb_clear(b);
    ingot::format_register<tagged>([](ingot::string_builder_t& b, const void* ptr) {
        const auto& t = *static_cast<const tagged*>(ptr);
        ingot::sb_format(b, ingot::str_lit("[{}, {}]"), t.id, ingot::str_from_cstr(t.name));
    });
    ingot::sb_format(b, ingot::str_lit("t: {}"), tagged{1, "a"});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("t: [1, a]")),
                  "overwritten registration");

    ingot::sb_destroy(b);
}
```

- [ ] **Step 5: 혼합 타입 테스트 추가**

```cpp
TEST_CASE("sb_format: mixed builtin and user-defined types") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("v={} n={} b={}"),
                     test_vec3{1, 2, 3}, 42, true);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v=(1, 2, 3) n=42 b=true")),
                  "mixed types");

    ingot::sb_destroy(b);
}
```

---

### Task 5: 빌드 및 테스트 검증

- [ ] **Step 1: Debug 빌드**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
Expected: Build succeeds with no errors.

- [ ] **Step 2: 테스트 실행**

```bash
ctest --test-dir build --output-on-failure
```
Expected: All tests pass, including new format tests.

- [ ] **Step 3: Commit**

```bash
git add ingot.h tests/test_string.cpp
git commit -m "feat: add user-defined type formatter registration to sb_format"
```

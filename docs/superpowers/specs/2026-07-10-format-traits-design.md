# ingot: 컴파일 타임 포매터 트레이트 및 컨테이너 확장 설계

**날짜:** 2026-07-10
**상태:** 설계 — 구현 대기
**범위:** `format_traits<T>` 컴파일 타임 트레이트 도입, `sb_format`/`ssb_format` 통합 사용자 정의 타입 지원, 컨테이너·뷰 타입 빌트인 포매터 제공

---

## 1. 동기 및 배경

### 1.1 현재 상태

2026-07-09 스펙에서 `format_dispatch<T>` 함수 포인터 기반 런타임 포매터 등록이 `sb_format`에만 추가되었다. `ssb_format`은 사용자 정의 타입을 지원하지 않으며 `static_assert`로 컴파일 에러가 발생한다.

```cpp
// 현재: sb_format 전용 런타임 디스패치
using format_fn_t = void (*)(string_builder_t& b, const void* ptr);
template <typename T> inline format_fn_t format_dispatch = nullptr;

// ssb_format 에서는 사용자 정의 타입 불가
template <int64_t N, typename T>
void format_one_ssb(...) {
    // ...
    } else {
        static_assert(sizeof(T) == 0, "ssb_format: unsupported type");
    }
}
```

### 1.2 문제점

1. **`ssb_format` 미지원**: `format_register_` 매크로로 등록한 타입도 `ssb_format`에서는 사용 불가
2. **런타임 오버헤드**: 함수 포인터 간접 호출로 인라인 불가
3. **컨테이너 타입 포매팅 부재**: `static_vector_t`, `view_t`, `string_t` 등 ingot 자체 타입에 대한 포매터가 없음

### 1.3 설계 원칙

- **컴파일 타임 디스패치**: 템플릿 특수화 기반으로 함수 포인터 없이 완전 인라인 가능
- **통합 등록**: `format_register_` 한 번으로 `sb_format`과 `ssb_format` 양쪽에서 동작
- **RTTI/레지스트리 불필요**: `format_traits<T>` 특수화 자체가 타입 식별자 역할
- **POD 전용**: 힙 할당, 가상 함수, 예외 없음

---

## 2. API 설계

### 2.1 `format_traits<T>` — 포매터 트레이트

```cpp
namespace ingot {

template <typename T>
struct format_traits {
    template <typename Builder>
    static void write(Builder& b, const T& val) {
        static_assert(sizeof(T) == 0,
            "No formatter registered for this type. "
            "Use format_register_(Type, fmt_str, ...) to register one.");
    }
};

} // namespace ingot
```

- `Builder` 템플릿 파라미터로 `string_builder_t&`와 `static_string_builder_t<N>&`를 자연스럽게 받음
- 미등록 타입 사용 시 `static_assert`로 컴파일 에러

### 2.2 `format_write()` — 통합 출력 헬퍼

`format_traits::write()` 내부에서 빌더 타입을 의식하지 않고 재귀 포매팅할 수 있도록 오버로드 제공:

```cpp
// 동적 빌더용
template <typename... Args>
void format_write(string_builder_t& b, string_t fmt, Args&&... args);

// 정적 빌더용
template <int64_t N, typename... Args>
void format_write(static_string_builder_t<N>& b, string_t fmt, Args&&... args);
```

각각 `sb_format(b, fmt, args...)` / `ssb_format(b, fmt, args...)` 로 포워딩한다.

### 2.3 `format_register_` 매크로

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

- **`Type`**: 등록할 사용자 정의 타입
- **`fmt_str`**: 포맷 문자열 리터럴
- **`...`**: placeholder를 채울 값들. `_v` 변수를 통해 타입 멤버에 접근
- **제약**: 템플릿 특수화이므로 **네임스페이스 또는 전역 스코프**에서만 사용 가능 (함수 내부 불가)
- **ADL**: 매크로 내 `format_write` 호출은 비한정 호출(unqualified call). `_B`가 `ingot` 네임스페이스의 빌더 타입이므로 ADL로 `ingot::format_write`를 찾음

#### 사용 예시

```cpp
#include "ingot.h"

struct vec3 { float x, y, z; };
format_register_(vec3, "({}, {}, {})", _v.x, _v.y, _v.z)

// sb_format
void foo() {
    string_builder_t b;
    sb_create(b, heap_alloc);
    sb_format(b, str_lit("pos: {}"), vec3{1, 2, 3});
    // 결과: "pos: (1, 2, 3)"
    sb_destroy(b);
}

// ssb_format — 동일한 format_register_ 로 동작
void bar() {
    static_string_builder_t<128> b;
    ssb_create(b);
    ssb_format(b, str_lit("pos: {}"), vec3{1, 2, 3});
    // 결과: "pos: (1, 2, 3)"
}
```

### 2.4 빌트인 컨테이너 포매터

`ingot.h`에 아래 `format_traits` 특수화를 미리 제공한다. 모든 시퀀스 포매터는 요소 타입 `T`에 대해 `format_write`를 통한 재귀 포매팅을 지원한다.

| 타입 | 출력 형식 | 비고 |
|---|---|---|
| `static_vector_t<T>` | `[e1, e2, e3]` | 빈 벡터는 `[]`, 요소 구분자 `", "` |
| `view_t<T>` | `[e1, e2, e3]` | 빈 뷰는 `[]`, 요소 구분자 `", "` |
| `string_t` | `hello` | 내용 그대로 출력 |
| `string_builder_t` | `hello [5/256]` | 내용 + `[len/capacity]` |
| `static_string_builder_t<N>` | `hello [5/256]` | 내용 + `[len/N]` |
| `utf8_rune_view_t` | `한글` | UTF-8 해석된 원문 텍스트 |

#### 재귀 포매팅 예시

```cpp
static_vector_t<vec3> points;
sv_push(points, vec3{1, 2, 3});
sv_push(points, vec3{4, 5, 6});

sb_format(b, str_lit("points: {}"), points);
// 결과: "points: [(1, 2, 3), (4, 5, 6)]"
```

---

## 3. 내부 구현

### 3.1 `format_one_sb` 수정

`detail::format_one_sb<T>` 의 `else` 분기에서 `format_traits<T>::write(b, val)` 호출:

```cpp
template <typename T>
void format_one_sb(string_builder_t& b, const void* ptr) {
    const T& val = *static_cast<const T*>(ptr);
    if constexpr (/* builtin types */) {
        // ...
    } else {
        format_traits<T>::write(b, val);
    }
}
```

### 3.2 `format_one_ssb` 수정

`detail::format_one_ssb<N, T>` 의 `else` 분기를 동일하게 변경:

```cpp
template <int64_t N, typename T>
void format_one_ssb(static_string_builder_t<N>& b, const void* ptr) {
    const T& val = *static_cast<const T*>(ptr);
    if constexpr (/* builtin types */) {
        // ...
    } else {
        format_traits<T>::write(b, val);  // 기존 static_assert 대체
    }
}
```

### 3.3 `format_write` 오버로드

`sb_format`과 `ssb_format` 정의 이후에 배치하여, `format_traits::write()`의 지연된 템플릿 인스턴스화 시점에 가시성 보장:

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

### 3.4 `format_write` forward declarations

`format_register_` 매크로 내 `format_write` 호출의 qualified name lookup을 위해, `format_traits<T>` 정의 직후에 전방 선언을 둔다:

```cpp
template <typename... Args>
void format_write(string_builder_t& b, string_t fmt, Args&&... args);

template <int64_t N, typename... Args>
void format_write(static_string_builder_t<N>& b, string_t fmt, Args&&... args);
```

실제 정의는 `sb_format`/`ssb_format` 이후에 배치된다 (3.3 참조). `format_register_` 매크로 내에서는 비한정 호출을 사용하므로 ADL로 찾지만, 전방 선언을 통해 qualified 호출도 가능하게 한다.

### 3.5 헤더 배치 순서

```
ingot.h:
  (1) format_traits<T> 기본 템플릿
  (2) format_write forward declarations
  (3) detail::format_one_sb<T>
  (4) detail::format_one_ssb<N, T>
  (5) format_register_ 매크로
  (6) sb_format / ssb_format
  (7) format_write 오버로드 구현
  (8) 빌트인 컨테이너 format_traits 특수화
```

### 3.6 빌트인 컨테이너 포매터 구현

각 컨테이너 포매터는 기존 public API로 요소에 접근하며, `format_write`를 통해 요소를 재귀 포매팅한다.

```
static_vector_t<T> → sv_count(), sv_begin()/sv_end()로 순회
view_t<T>          → view_len(), view_data()로 순회
string_t           → 내용을 그대로 append
string_builder_t   → sb_to_string(), sb_len(), sb_capacity()
static_string_builder_t<N> → ssb_to_string(), ssb_len(), N
utf8_rune_view_t   → utf8_rune_view_begin/cursor 순회로 rune 디코딩
```

시퀀스 포매터 예시 (의사 코드):

```cpp
template <typename T>
struct format_traits<static_vector_t<T>> {
    template <typename B>
    static void write(B& b, const static_vector_t<T>& v) {
        format_write(b, str_lit("["));
        for (int64_t i = 0; i < sv_count(v); ++i) {
            if (i > 0) format_write(b, str_lit(", "));
            format_write(b, str_lit("{}"), *sv_begin(v, i));  // 재귀 포매팅
        }
        format_write(b, str_lit("]"));
    }
};
```

### 3.7 제거되는 요소

| 제거 대상 | 사유 |
|---|---|
| `format_fn_t` | 함수 포인터 타입 불필요 |
| `format_dispatch<T>` | 템플릿 변수 기반 디스패치 제거 |
| `format_register<T>(fn)` | 런타임 등록 API 제거 |
| `format_one_ssb`의 `static_assert` | `format_traits<T>::write` 호출로 대체 |

---

## 4. 에러 처리

| 상황 | 처리 |
|---|---|
| 미등록 타입을 `sb_format`/`ssb_format`에 전달 | 컴파일 에러: `static_assert(sizeof(T)==0, "...")` |
| `format_register_` 매크로에서 placeholder-인자 불일치 | `sb_format`/`ssb_format` 내부 `ingot_assert_`로 런타임 검출 |
| 함수 내부에서 `format_register_` 사용 | 컴파일 에러: `templates can only be declared in namespace or class scope` |
| 동일 타입에 `format_register_` 중복 사용 | 컴파일 에러: explicit specialization 재정의 (ODR 위반) |

---

## 5. 범위 외 (Out of Scope)

- **런타임 포매터 변경/덮어쓰기** — 컴파일 타임 특수화는 재등록 불가
- **Placeholder별 포맷 옵션 (`{:.2f}`, `{:x}`)** — 별도 스펙
- **컨테이너 구분자/브래킷 커스터마이즈** — 현재는 기본값 `[a, b]` 고정
- **`str_format` (독립형 반환 함수)** — 로깅 API 부재로 여전히 범위 외

---

## 6. 이전 스펙과의 차이점

| 항목 | 2026-07-09 (이전) | 2026-07-10 (현재) |
|---|---|---|
| 디스패치 방식 | 함수 포인터 (동적) | 템플릿 특수화 (컴파일 타임) |
| `ssb_format` 지원 | 미지원 | 지원 |
| 등록 API | `format_register<T>(fn)` | `format_register_` 매크로만 |
| 덮어쓰기 | 가능 | 불가 |
| 컨테이너 포매터 | 없음 | 6종 빌트인 제공 |
| 재귀 포매팅 | 불가 | `format_write`로 지원 |
| 인라인 가능성 | 함수 포인터로 인해 제한적 | 완전 인라인 가능 |

---

## 7. 설계 결정 요약

| 결정 | 선택 | 근거 |
|---|---|---|
| 디스패치 방식 | `format_traits<T>` 특수화 | 제로 오버헤드, `ssb_format` 자연스러운 지원 |
| 등록 API | `format_register_` 매크로 | 한 번 등록으로 양쪽 빌더 지원 |
| 빌더 추상화 | 템플릿 파라미터 `Builder` | `format_writer_t` 없이 컴파일 타임 다형성 |
| 컨테이너 출력 형식 | `[e1, e2]`, `"str" [5/256]` | 직관적, 디버깅에 유용 |
| 런타임 API | 제거 | YAGNI — 덮어쓰기 기능 불필요 |

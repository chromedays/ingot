# ingot: 포매팅 시스템 사용자 정의 타입 확장 설계

**날짜:** 2026-07-09
**상태:** 설계 — 구현 대기
**범위:** `sb_format` 에 사용자 정의 타입에 대한 동적 포매터 등록 기능을 추가한다.

---

## 1. 동기 및 배경

현재 `sb_format` 과 `ssb_format` 은 `string_t`, `const char*`, `char`, `bool`, `char32_t`, 부동소수점, 정수 타입만 지원한다. 이 타입들이 아닌 인자를 전달하면 `static_assert` 로 컴파일 에러가 발생한다.

```cpp
struct vec3 { float x, y, z; };
ingot::sb_format(b, ingot::str_lit("pos: {}"), vec3{1,2,3});
// static_assert(sizeof(T)==0) → 컴파일 에러
```

사용자가 자신의 타입에 대한 포매터를 등록할 수 있도록 하여 `sb_format` 을 확장 가능하게 만든다.

### 1.1 설계 원칙

- **동적 디스패치**: 함수 포인터 기반으로 런타임에 포매터를 등록/변경할 수 있다. 타입별로 하나의 함수 포인터만 저장한다.
- **RTTI 불필요**: 템플릿 정적 변수 `format_dispatch<T>` 의 인스턴스화 자체가 타입 식별자 역할을 하므로 `typeid` 나 `type_index` 가 필요하지 않다.
- **레지스트리/배열 없음**: 별도의 전역 레지스트리 배열, 선형 검색, 동적 할당이 없다. `format_dispatch<T>` 는 템플릿 변수이므로 컴파일 타임에 타입별로 하나씩 존재하며, O(1) 조회다.
- **`sb_format` 전용**: `ssb_format` 은 이번 스펙에서 확장하지 않는다. 필요시 별도 스펙.

---

## 2. API 설계

### 2.1 핵심 타입 및 변수

```cpp
namespace ingot {

using format_fn_t = void (*)(string_builder_t& b, const void* ptr);

template <typename T>
inline format_fn_t format_dispatch = nullptr;

}
```

- **`format_fn_t`**: 포매터 함수 시그니처. `string_builder_t` 참조와 타입 소거된 `const void*` 포인터를 받는다.
- **`format_dispatch<T>`**: 타입 `T` 에 대한 포매터 함수 포인터. 템플릿 인스턴스화로 `T` 별로 고유한 정적 변수가 생성된다. 초깃값은 `nullptr`.

### 2.2 등록 함수

```cpp
template <typename T>
void format_register(format_fn_t fn);
```

- `format_dispatch<T>` 에 `fn` 을 저장한다.
- 사용자가 직접 호출하거나 헬퍼 매크로를 통해 호출한다.
- 동일 타입에 대해 여러 번 호출하면 마지막 등록이 유효하다 (덮어쓰기).

### 2.3 헬퍼 매크로

```cpp
#define format_register_(Type, fmt_str, ...)
```

- **`Type`**: 등록할 사용자 정의 타입
- **`fmt_str`**: `sb_format` 에 전달할 포맷 문자열 리터럴 (예: `"({}, {}, {})"`)
- **`...`**: 포맷 문자열의 placeholder 를 채울 값들. 내부 변수 `_v` (등록된 타입의 `const` 참조) 를 통해 타입 멤버에 접근한다.
- 전역 범위에서 사용 시 정적 변수 초기화를 통해 `main()` 이전에 자동 등록된다.

### 2.4 사용 예시

```cpp
#include "ingot.h"

struct vec3 { float x, y, z; };

// 전역 범위 등록 — main() 이전에 자동 실행
format_register_(vec3, "({}, {}, {})", _v.x, _v.y, _v.z)

int main() {
    ingot::string_builder_t b;
    ingot::sb_create(b, ingot::heap_alloc);

    ingot::sb_format(b, ingot::str_lit("pos: {}"), vec3{1, 2, 3});
    // 결과: "pos: (1, 2, 3)"

    char buf[256];
    ingot::string_t s = ingot::sb_to_string(b, buf);
    ingot::log_info("{}", s); // 로그 출력

    ingot::sb_destroy(b);
}
```

---

## 3. 내부 구현

### 3.1 `format_one_sb` 수정

`detail::format_one_sb<T>` 의 `if constexpr` 체인의 `else` 분기에서 런타임 디스패치를 시도한다:

```cpp
template <typename T>
void format_one_sb(string_builder_t& b, const void* ptr) {
    const T& val = *static_cast<const T*>(ptr);
    if constexpr (std::is_same_v<T, string_t>) {
        sb_append(b, val);
    } else if constexpr (/* ... 기존 빌트인 타입 분기 ... */) {
        // ...
    } else if constexpr (std::is_integral_v<T>) {
        // ...
    } else {
        if (format_dispatch<T> != nullptr) {
            format_dispatch<T>(b, ptr);
        } else {
            ingot_assert_(false, "sb_format: unsupported type (no formatter registered)");
        }
    }
}
```

- 빌트인 타입 분기는 그대로 유지 (컴파일 타임 디스패치)
- `else` 분기: 런타임에 `format_dispatch<T>` 확인
  - 등록됨 → 함수 포인터 호출
  - 미등록 → `ingot_assert_` 실패 (기존 `static_assert` 에서 런타임 assertion 으로 변경)
- **주의**: `format_dispatch<T> != nullptr` 은 런타임 조건이므로 `if constexpr` 체인 바깥에서 일반 `if` 로 검사한다. `else if constexpr` 에 넣으면 조건이 상수식이 아니므로 비대상 분기도 인스턴스화되어 `static_assert` 가 강제로 발동하기 때문.

### 3.2 `ssb_format` 영향 없음

`format_one_ssb<N, T>` 와 `ssb_format` 은 수정하지 않는다. `static_string_builder_t` 에 대한 사용자 정의 타입 확장은 향후 별도 스펙에서 다룬다.

### 3.3 헤더/구현 분리

- `format_fn_t`, `format_dispatch<T>` (템플릿 변수), `format_register<T>` (템플릿 함수), `format_register_` 매크로 — 모두 `ingot.h` 템플릿 영역에 인라인 배치
- 별도 `.cpp` 구현 불필요 (템플릿/인라인 전용)

---

## 4. 에러 처리

| 상황 | 처리 |
|---|---|
| 미등록 타입을 `sb_format` 에 전달 | `ingot_assert_` 실패 — 런타임 에러 (메시지: `"sb_format: unsupported type (no formatter registered)"`) |
| `format_register_` 매크로에서 placeholder-인자 불일치 | `sb_format` 내부 `ingot_assert_` 로 런타임 검출 |
| `format_register_` 매크로 타입 불일치 | `_v` 캐스팅은 `static_cast` — 포인터 오용 시 미정의 동작

---

## 5. 범위 외 (Out of Scope)

- **`ssb_format` 사용자 정의 타입 지원** — 필요 시 별도 스펙
- **컴파일 타임 포매터 검출 (ADL, `concept`)** — 이번 스펙은 동적 디스패치만 다룬다
- **`str_format` (독립형 반환 함수)** — 로깅 API 부재로 여전히 범위 외
- **레지스트리 해제 (`format_unregister`)** — 필요 없음 (덮어쓰기로 충분)
- **Placeholder 별 포맷 옵션 (`{:.2f}`, `{:x}`)** — 별도 스펙

---

## 6. 코딩 표준 준수 요약

| 항목 | 적용 |
|---|---|
| 매크로명 | `format_register_` — snake_case + `_` 접미사 |
| 타입명 | `format_fn_t` — snake_case + `_t` 접미사 |
| 함수명 | `format_register` — `format_` 모듈 접두사 |
| 템플릿 변수명 | `format_dispatch` — snake_case |
| 함수 스타일 | 자유 함수, 주체를 첫 인자로 |
| 에러 처리 | `ingot_assert_` (매크로 내부 `sb_format` 에서), 예외 없음 |
| POD 검증 | 새 구조체 없음 (`format_fn_t` 는 함수 포인터 alias) |

---

## 7. 설계 결정 요약

| 결정 | 선택 | 근거 |
|---|---|---|
| 디스패치 방식 | 함수 포인터 (동적) | 런타임 등록/변경 가능, RTTI 불필요 |
| 타입 식별 | 템플릿 정적 변수 주소 | O(1) 조회, 별도 레지스트리 배열 없음 |
| 등록 방식 | 정적 변수 초기화 | `main()` 이전 자동 실행, 힙 할당 없음 |
| 매크로 형태 | `format_register_(Type, fmt, ...)` | `_v` 변수 자동 바인딩, 캐스팅/보일러플레이트 제거 |
| `ssb_format` 확장 | 범위 외 | YAGNI — 필요 시 별도 스펙 |

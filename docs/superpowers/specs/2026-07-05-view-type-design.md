# ingot: 뷰 타입 설계

**날짜:** 2026-07-05
**상태:** 설계 — 구현 대기
**범위:** `ingot` 라이브러리에 제네릭 비소유 뷰 타입 `view_t<T>`를 추가. 기존 `string_t`가 `const char`에 특화된 형태의 동일 패턴이라면, `view_t<T>`는 임의의 POD 타입 `T`에 대한 일반화.

---

## 1. 동기 및 배경

`ingot`에는 이미 비소유 뷰 패턴이 두 곳에 등장한다.

- `string_t { const char* data; int64_t len }` — 바이트 시퀀스 뷰
- `static_vector_t<T>::data` / `count` — 소유 컨테이너지만 원소 버퍼를 외부에 노출할 일관된 수단이 없음

임의 타입 `T`의 연속된 메모리를 함수 경계로 안전하게 전달하는 제네릭 뷰 타입이 필요하다. 본 설계는 EASTL, stb_ds, Odin, Zig의 뷰/슬라이스 접근을 조사한 뒤 **Odin/Zig 스타일의 `{ptr, len}` 2워드 비소유 뷰**를 채택한다.

### 1.1 조사 요약

| 라이브러리 | 뷰 표현 | 크기 | cap | 바운드 체크 | 비고 |
|---|---|---|---|---|---|
| **EASTL** `span<T>` (dynamic) | `{T*, size_t}` | 2 words | 없음 | debug assert | `std::span`의 게임엔진 튜닝판. fixed extent `span<T,N>`은 1워드 |
| **EASTL** `span<T,N>` (fixed) | `{T*}`만 | 1 word | — | debug assert | size를 컴파일타임 상수로 인코딩 |
| **Odin** `[]T` | `{data, len}` | 2 words | 없음 | 언어 내장 + opt-out | `[dynamic]T`가 별도로 `{data,len,cap,allocator}` (4 words) |
| **Zig** `[]T` | `{ptr, len}` | 2 words | 없음 | safe 모드 panic | 센티넬 슬라이스 `[:0]T` 별도. `ArrayList`가 cap 관리 |
| **stb_ds.h** | (뷰 타입 없음) | — | — | 없음 | "배열 포인터 == 뷰". 헤더를 `T*-1`에 숨김 |

**핵심 통찰:**

1. **현대 설계는 모두 `{ptr, len}` 2워드로 수렴하며, cap은 소유 컨테이너에만 둔다.** Go의 `{ptr,len,cap}` 3워드 모델은 더 이상 따르지 않는다. ingot의 기존 `string_t`가 정확히 이 패턴.
2. **뷰와 소유 컨테이너는 타입 수준에서 분리된다.** Odin(`[]T` vs `[dynamic]T`), Zig(`[]T` vs `ArrayList`), EASTL(`span` vs `vector`) 모두 그렇다. ingot도 `view_t<T>`(비소유) vs `static_vector_t<T>`(소유) 분리를 유지한다.
3. **stb_ds가 뷰를 거부한 이유(C에 수명/소유권 도구 부재)는 C++에 해당하지 않는다.** 값 시맨틱스와 명시적 수명 관리가 있는 C++에서는 `{ptr,len}` 뷰가 false safety 없이 유용하다.
4. **EASTL의 fixed extent 1워드 최적화는 흥미롭지만 복잡도 대비 이득이 작다.** 단일 동적 extent 타입으로 단순성을 취한다(YAGNI).

### 1.2 채택한 방향

- **동적 extent 단일 타입**: `view_t<T> = {T* data; int64_t len}`. fixed extent 특수화 없음.
- **`string_t`와 독립 유지**: 별칭 통합이나 제거가 아닌, 별개 타입으로 공존. 약간의 중복 감수(레이아웃/함수) but 각 타입의 의미가 명확.
- **POD 필수**: `T`는 `is_trivially_copyable && is_standard_layout`. `static_vector_t`와 라이브러리 철학(pod_containers) 일관.

---

## 2. 타입 개요

`namespace ingot`에 제네릭 뷰 타입 1종을 추가한다. `static_assert`로 POD 검증.

| 타입 | 역할 | 레이아웃 | 소유? |
|---|---|---|---|
| **`view_t<T>`** | 임의 POD `T` 시퀀스의 비소유 뷰 | `{T* data; int64_t len}` | 아니오 |

**자유 함수 접두사** (코딩 표준 준수): `view_` — `view_t`에서 `_t` 제거. 축약 불필요(4글자).

> **접두사 충돌 참고**: 기존 접두사들(`sv_`, `str_`, `sb_`, `ssb_`, `utf8_`)과 `view_`는 명확히 구분됨.

---

## 3. `view_t<T>` 상세

### 3.1 레이아웃

```cpp
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
```

16바이트(64비트), trivially copyable. 값 전달이 효율적(두 레지스터). `string_t`와 동일 레이아웃이지만 `T`가 가변(`string_t`는 `const char` 고정).

### 3.2 연산

```cpp
// === 팩토리 (view_from 오버로드로 통일) ===
//   팩토리는 view_t를 "생산"하므로 첫 인자가 view_t가 아님.
//   str_from(data, len)이 string_t 생산에 str_ 접두사를 쓰는 예외와 동일.
template<typename T>
view_t<T> view_from(T* data, int64_t len);

template<typename T, int64_t N>
view_t<T> view_from(T (&arr)[N]);                          // C 배열

template<typename T>
view_t<T> view_from(static_vector_t<T>& sv);               // mutable → view_t<T>

template<typename T>
view_t<const T> view_from(const static_vector_t<T>& sv);   // const → view_t<const T>

// === 검사 ===
//   단일 템플릿으로 T 추론이 mutable/const 모두 처리:
//   view_t<int>       → T=int,       view_data 반환 int*
//   view_t<const int> → T=const int, view_data 반환 const int*
template<typename T> int64_t   view_len(view_t<T> v);
template<typename T> bool      view_is_empty(view_t<T> v);
template<typename T> T*        view_data(view_t<T> v);

// === 접근 ===
//   ingot_assert_(0 <= index < len) 범위 검사.
//   view_t<T>       → T& 반환 (수정 가능)
//   view_t<const T> → const T& 반환 (읽기 전용)
template<typename T> T&        view_at(view_t<T> v, int64_t index);

// === 슬라이스 ===
//   반개구간 [low, high). ingot_assert_(0 <= low <= high <= len).
//   반환 타입은 입력과 동일: view_t<T> → view_t<T>, view_t<const T> → view_t<const T>
template<typename T> view_t<T> view_slice(view_t<T> v, int64_t low, int64_t high);

// === 반복 ===
template<typename T> T*        view_begin(view_t<T> v);
template<typename T> T*        view_end(view_t<T> v);

// ADL — range-for 지원 (std::begin/end와 충돌 회피: 인자가 view_t<T>일 때만 후보)
template<typename T> T*        begin(view_t<T> v);
template<typename T> T*        end(view_t<T> v);
```

### 3.3 설계 노트

- **`view_from` 오버로드 통일**: C++ 오버로딩으로 모든 생성 경로를 `view_from` 이름 아래 통합. 코딩 표준의 "첫 인자 접두사" 규칙은 해당 타입을 조작/관찰하는 함수(`sv_push`, `view_at`)를 위한 것이며, 팩토리(값을 생산하는 함수)는 `str_from`이 이미 설정한 예외를 따른다. 생산 타입의 접두사(`view_`) 사용.
- **mutable/const 오버로드**: `view_t<T>`는 요소 수정 가능, `view_t<const T>`는 읽기 전용. `view_from(sv)`의 두 오버로드로 `static_vector_t<T>&` → `view_t<T>`, `const static_vector_t<T>&` → `view_t<const T>` 자동 분기(const-정확성).
- **반복자는 포인터**: `T*` 그 자체. `std::sort` 등 표준 알고리즘과 제로 오버헤드 호환. EASTL span과 동일.
- **`view_at`은 `ingot_assert_`로 범위 검사**: `static_vector_t::operator[]` / `str_at`과 동일 철학. 자유 함수로(operator 예외 제외하고 멤버 함수 금지 규칙 준수).
- **`view_slice`은 zero-copy 부분 뷰**: `{v.data + low, high - low}` 반환. `str_slice`와 동일 패턴.
- **모두 O(1)**.

---

## 4. `string_t`와의 관계 (독립 유지)

`string_t { const char* data; int64_t len }`은 사실상 `view_t<const char>`와 동일 레이아웃이지만, **별개 타입으로 유지**한다.

**근거:**

1. **의미적 명확성**: `string_t`는 "바이트 시퀀스(문자열)"라는 도메인 의미. `str_equal`(memcmp), `str_from_cstr`(strlen), `str_lit`(리터럴), UTF-8 헬퍼 통합 등 문자열 전용 연산이 타입에 결부됨. `view_t<const char>`는 "char의 단순 시퀀스"라 범용 의미.
2. **중복은 작다**: 레이아웃 중복(2필드)과 `at`/`slice`/`begin`/`end`/`len` 등 기본 연산 중복은 미미. 문자열 전용 연산(`str_equal`, `str_from_cstr`, `str_lit`, `utf8_*`)은 어느 쪽이든 `string_t`에만 속함.
3. **별칭(`using string_t = view_t<const char>`)의 문제**: `str_*` 함수들이 첫 인자로 `string_t`(= `view_t<const char>`)를 받게 되면, `view_*` 함수와 `str_*` 함수가 같은 타입에 대해 다른 접두사로 혼재. ADL `begin`/`end`도 충돌(동일 시그니처 재정의). 독립 타입이 이런 모호성을 제거.

**상호운용**: 필요 시 수동 변환 — `view_from(str_data(s), str_len(s))`로 `string_t` → `view_t<const char>` 획득. 빌더 → 뷰는 기존 `sb_to_string` 유지.

---

## 5. 에러 처리 (예외 없음 — 모두 `ingot_assert_`)

| 연산 | assertion |
|---|---|
| `view_from(data, len)` | `len >= 0`, `data != nullptr \|\| len == 0` |
| `view_at(v, i)` | `0 <= index < len` |
| `view_slice(v, low, high)` | `0 <= low <= high <= len` |

모든 실패는 `ingot_assert_` (`str_at`, `sv_push`와 일관). 부드러운 실패 경로 없음.

> **암묵적 사전 조건**: 모든 길이/인덱스 인자는 `>= 0`. 음수는 `ingot_assert_`로 거부.

---

## 6. 불변 조건 (Invariants)

- **`view_t<T>`**: 비소유. 할당자 없음. 호출자가 `data`가 뷰보다 오래 살도록 보장.
- **`len >= 0`**: 항상. 빈 뷰는 `{nullptr, 0}` 또는 `{non-null, 0}`.
- **POD**: `view_t<T>` 자체가 trivially copyable + standard layout이며, `T`도 그래야 함.

---

## 7. 테스팅

코딩 표준 + doctest 스펙을 따른다. 신규 TU `tests/test_view.cpp` 추가.

### 7.1 테스트 파일 구조

```
tests/
├── doctest.h               # (기존)
├── doctest_main.cpp        # (기존)
├── test_static_vector.cpp  # (기존)
├── test_string.cpp         # (기존)
├── test_view.cpp           # 신규 — view_t 테스트
└── CMakeLists.txt          # test_view 실행 파일 + doctest_main 링크 추가
```

`tests/CMakeLists.txt`에 추가:

```cmake
add_executable(test_view test_view.cpp doctest_main.cpp)
target_link_libraries(test_view PRIVATE ingot)
add_test(NAME test_view COMMAND test_view)
```

### 7.2 테스트 커버리지

`test_view.cpp`의 `TEST_CASE` 목록 (doctest `CHECK`/`REQUIRE` 스타일):

**생성 (`view_from`):**
- raw ptr + len (정상, 빈 뷰, len=0 with non-null ptr)
- C 배열 (`int arr[] = {1,2,3}; view_from(arr)`)
- `static_vector_t<T>&` → `view_t<T>` (mutable, 요소 수정 가능)
- `const static_vector_t<T>&` → `view_t<const T>` (읽기 전용, 요소 수정 컴파일 에러 확인은 생략 가능)
- POD가 아닌 `T`에 대한 `static_assert` 발생 검증 — 컴파일 타임 검증이므로 런타임 `TEST_CASE`가 아닌, 별도의 음성 컴파일 체크(주석으로 문서화된 컴파일 에러 예시)로 확인

**검사:**
- `view_len`, `view_is_empty`, `view_data`

**접근:**
- `view_at` (정상 인덱스, 경계 `len-1`)
- `view_at` mutable/const 동작 (`view_t<int>` → 수정 가능, `view_t<const int>` → 읽기 전용)

**슬라이스:**
- `view_slice` (정상 부분, 전체 `view_slice(v, 0, view_len(v))`, 빈 `view_slice(v, i, i)`)
- 슬라이스의 슬라이스

**반복:**
- `view_begin`/`view_end` 포인터 range-for
- ADL `begin`/`end`로 `for (auto& x : v)` 동작

### 7.3 assert 검증 참고

`ingot_assert_`는 `std::abort()`로 종료되므로 doctest가 직접 포착 불가 (string 스펙 §10.3 참고). 범위 초과 `view_at`/`view_slice`의 assert는 사전 조건 함수로 간접 검증하거나 별도 프로브로 확인.

---

## 8. 파일 구성 — 단일 파일 유지

`view_t<T>`과 `view_*` 함수는 기존 **`ingot.h` / `ingot.cpp`**에 추가한다. 별도 파일로 분리하지 않는다.

**근거:**
- 프로젝트 규칙(단일 라이브러리 헤더/소스)과 일치.
- `view_t`는 템플릿이므로 대부분 헤더에 인라인. `ingot.cpp`에 추가할 구현은 거의 없음(모든 `view_*`가 짧은 접근자라 헤더 인라인).

`ingot.h` 구성 순서 (`static_vector_t` 직후, `string_t` 이전에 배치 — 제네릭이 문자열 특화보다 기초적):

```cpp
namespace ingot {

// ... 기존: allocator_t, heap_allocator_t, arena_allocator_t ...

template<typename T>
struct static_vector_t { ... };
// sv_* 함수

// === 뷰 타입 === (신규)
template<typename T>
struct view_t { ... };
// view_from 오버로드, view_* 함수

// === 스트링 타입 === (기존)
struct string_t { ... };
// str_*, sb_*, ssb_*, utf8_*

} // namespace ingot
```

---

## 9. 범위 외 (Out of Scope)

본 설계에서 의도적으로 제외 (YAGNI — 필요 시 별도 스펙):

- **Fixed extent `view_t<T, N>`** — EASTL `span<T,N>` 스타일 1워드 최적화. 단일 동적 extent로 충분; 복잡도 대비 이득 작음.
- **`view_first(v, n)` / `view_last(v, n)` / `view_from_offset(v, off)` 단축 함수** — `view_slice(v, 0, n)` / `view_slice(v, len-n, len)` / `view_slice(v, off, len)`로 커버.
- **`view_front` / `view_back`** — `view_at(v, 0)` / `view_at(v, len-1)`로 커버.
- **`view_contains(v, val)` / `view_find` / `view_equals`** — 탐색/비교 연산. 필요 시 별도.
- **`view_as_bytes` / `view_from_bytes`** — `view_t<T>` ↔ `view_t<const std::byte>` 변환. `reinterpret_cast` 한 줄로 사용자가 작성 가능; 타입 펀닝 정책이 명확해진 후 별도 도입.
- **센티널 종료 뷰** — Zig `[:0]T` 스타일. ingot은 NUL 종료 정책이 순수 바이트(string 스펙 §7)이므로 불필요.
- **`string_t`를 `view_t<const char>`로 통합/제거** — §4에서 결정한 바와 같이 독립 유지.
- **`static_vector_t<T>`의 `operator[]`를 `view_t<T>`로 일반화** — 기존 멤버 `operator[]` 유지; 뷰는 별도 변환 경로(`view_from(sv)`)로 접근.

---

## 10. 설계 결정 요약

| 결정 | 선택 | 근거 |
|---|---|---|
| 레이아웃 | `{T* data; int64_t len}` 2워드, cap 없음 | EASTL/Odin/Zig 공통 합의; 기존 `string_t`와 동일 패턴 |
| Extent | 동적 단일 (fixed extent 특수화 없음) | 단순성; 1워드 절약 대비 템플릿 복잡도 과다 |
| `string_t` 관계 | 독립 타입 유지 (별칭/제거 안 함) | 의미적 명확성; 접두사/ADL 충돌 회피 |
| 요소 타입 제약 | POD 필수 (`static_assert`) | `static_vector_t` 및 라이브러리 철학(pod_containers) 일관 |
| API 범위 | 최소 (생성/검사/접근/슬라이스/반복) | YAGNI; 편의 함수는 `view_slice` 조합으로 커버 |
| 팩토리 네이밍 | `view_from` 오버로드 통일 (C++ 오버로딩) | `str_from` 팩토리 예외와 일관; 관용적 C++; 이름 기억 부담 최소 |
| `static_vector` → view | `view_from(sv)` 오버로드 (mutable/const) | 팩토리 통일의 일부; const-정확성 자동 분기 |
| 접두사 | `view_` | `view_t`에서 `_t` 제거; 기존 접두사와 충돌 없음 |
| 필드명 | `.data` / `.len` | `string_t`와 일관; designated initializer 가독성 |
| 에러 처리 | 모두 `ingot_assert_` | 예외 없음, 기존 코드와 일관 |
| 파일 구성 | 단일 파일 (`ingot.h`/`ingot.cpp`) | 현재 규칙 유지 |
| 테스트 구조 | `test_view.cpp` 신규 TU + `doctest_main.cpp` 링크 | 기존 테스트 구조 일관 |

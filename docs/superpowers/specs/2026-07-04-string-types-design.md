# ingot: 스트링 타입 설계

**날짜:** 2026-07-04
**상태:** 설계 — 구현 대기
**범위:** `ingot` 라이브러리에 뷰-퍼스트(view-first) 스트링 타입 3종(`string_t`, `string_builder_t`, `static_string_builder_t<N>`)과 옵션 UTF-8 헬퍼 모듈을 추가.

---

## 1. 동기 및 배경

현재 `ingot`은 `static_vector_t<T>` 하나만 제공한다. 텍스트 처리를 위해 바이트 시퀀스를 다루는 타입이 필요하다. 본 설계는 stb_ds, EASTL, Odin, Zig 의 스트링 설계를 조사한 뒤, **Odin/Zig 스타일의 뷰-퍼스트 접근**을 채택한다.

### 1.1 조사 요약

| 라이브러리 | 주 타입 | 소유권 | SSO | 인코딩 |
|---|---|---|---|---|
| stb_ds | `char*` (히든 헤더) | 소유 | 없음 | raw bytes |
| EASTL | `basic_string<T,Alloc>` (소유) | 소유 | 23문자 (union, 비트 훔치기) | raw bytes |
| Odin | `string` = `{ptr,len}` (뷰) | 비소유 | 없음 | UTF-8 관례 |
| Zig | `[]const u8` (뷰) | 비소유 | 없음 | byte / UTF-8 옵션 |

**핵심 통찰:**

1. **현대 시스템 언어(Odin, Zig)는 비소유 `(ptr, len)` 뷰를 주 타입으로 삼고, 소유/가능한 컨테이너는 제네릭 바이트 배열을 재사용**한다. SSO 없음, 소멸자 없음, 타입 자체에 할당자 필드 없음.
2. **어떤 라이브러리도 타입 수준에서 UTF-8을 강제하지 않는다.** 모두 바이트 저장이며 UTF-8은 라이브러리 연산(검증/디코드)으로 옵션. `len`은 항상 바이트 수. (Odin 의 `utf8.valid_string(s)` 가 존재한다는 사실 자체가 타입이 강제하지 않는다는 증거 — Rust 의 `str` 에는 대응 함수가 없다.)
3. **EASTL `fixed_string<N>`** 과 Odin `builder_from_bytes` 모두 "호출자 버퍼 위에서 동작하는 고정 용량 스트링"을 같은 방식으로 해결한다.

### 1.2 채택한 방향

- **뷰-퍼스트**: 비소유 뷰를 주 스트링 타입으로, 소유/가능한 타입은 별도의 빌더.
- **UTF-8 by convention**: `len`은 항상 바이트 수, UTF-8 검증/디코드는 옵션.
- **NUL 종료 없음 (Option 2)**: 순수 바이트. C 상호운용은 명시적 복사. (Odin/Zig 철학 일관성 — 자세한 근거는 §7)

---

## 2. 타입 개요

`namespace ingot` 에 세 타입과 UTF-8 헬퍼 모듈을 추가한다. 모두 `static_assert` 로 POD 검증.

| 타입 | 역할 | 레이아웃 | 소유? | 확장? | NUL? |
|---|---|---|---|---|---|
| **`string_t`** | 주 스트링 — 비소유 뷰 | `{const char* data; int64_t len}` | 아니오 | 아니오 | 아니오 |
| **`string_builder_t`** | 소유, 확장 가능, 힙/아레나 기반 | `{char* data; int64_t len; int64_t capacity; allocator_t* alloc}` | 예 (`allocator_t*`) | 예 | 아니오 |
| **`static_string_builder_t<N>`** | 소유, 고정 인라인 버퍼, 하드 캡 | `{char buffer[N]; int64_t len}` | 예 (인라인) | 아니오 (오버플로우 시 assert) | 아니오 |

**자유 함수 접두사** (코딩 표준 준수):

- `str_` — `string_t` (뷰) 연산
- `sb_` — `string_builder_t` (힙 소유자) 연산
- `ssb_` — `static_string_builder_t<N>` (스택 소유자) 연산
- `utf8_` — 옵션 UTF-8 헬퍼

> **접두사 충돌 참고**: `static_vector_t<T>` 의 기존 접두사 `sv_` 와 `string_t` 의 `str_` 은 구분된다 (`sv_` = static vector, `str_` = string).

**일관된 바이트 의미론:**

- 세 타입 모두 `len` 은 바이트 수.
- 예약 +1 용량 없음. 변경 시 NUL 기록부 부담 없음.
- `sb_to_string(b)` 로 얻은 뷰는 O(1), 빌더 버퍼를 직접 가리킴 — 빌더가 변경/확장/파괴되기 전까지 유효. (유일한 수명 규칙, §9 참고)

---

## 3. `string_t` (뷰)

### 3.1 레이아웃

```cpp
struct string_t {
    const char* data;
    int64_t     len;
};
static_assert(std::is_trivially_copyable_v<string_t> && std::is_standard_layout_v<string_t>,
              "string_t must be POD");
```

16바이트, trivially copyable. 호출 규약상 항상 **값 전달** (Odin/Zig 슬라이스 관례와 일치 — 두 레지스터에 들어감).

### 3.2 연산

```cpp
// 생성
string_t str_from(const char* data, int64_t len);          // ptr + 길이
string_t str_from_cstr(const char* s);                     // strlen 기반, O(n)
template <int64_t N>
constexpr string_t str_lit(const char (&s)[N]);            // 리터럴 전용, 타입 안전

// 검사
int64_t     str_len(string_t s);                           // O(1) == s.len
bool        str_is_empty(string_t s);
const char* str_data(string_t s);
char        str_at(string_t s, int64_t index);             // 범위 검사 ingot_assert_

// 비교
bool str_equal(string_t a, string_t b);                    // 바이트 단위 memcmp

// 슬라이스
string_t str_slice(string_t s, int64_t begin, int64_t end); // zero-copy 부분 뷰

// 바이트 반복
const char* str_begin(string_t s);
const char* str_end(string_t s);
const char* begin(string_t s);                             // ADL — range-for 지원
const char* end(string_t s);                               // ADL
```

### 3.3 설계 노트

- **생성은 값 반환** (16바이트 POD) — 제자리 `str_create(s, ...)` 보다 인체공학적. 빌더는 제자리 패턴(`sb_create(b, ...)`)을 써서 `sv_create` 와 일치.
- **`str_lit`** 는 `const char (&)[N]` 을 받는 함수 템플릿 — 타입 안전(문자열 리터럴/배열만 허용), `constexpr`, 매크로 함정 없음. `str_lit("hi")` → `{.data="hi", .len=2}` 컴파일 타임.
- **`str_at`** 은 `ingot_assert_` 로 범위 검사 — `static_vector_t::operator[]` 철학이지만 자유 함수로 (코딩 표준: 멤버 함수 금지, operator 예외 제외).
- **바이트 반복** 은 ADL `begin`/`end` 로 `for (char c : view)` 지원. rune 단위 반복은 `utf8_*` (§6).
- **모두 O(1)** — 단, `str_from_cstr` (strlen), `str_equal` (memcmp) 은 O(n).

---

## 4. `string_builder_t` (힙 소유자)

### 4.1 레이아웃

```cpp
struct string_builder_t {
    char*        data;
    int64_t      len;
    int64_t      capacity;
    allocator_t* alloc;
};
static_assert(std::is_trivially_copyable_v<string_builder_t> && std::is_standard_layout_v<string_builder_t>,
              "string_builder_t must be POD");
```

`static_vector_t` 의 형태와 동일 — `{data, len, capacity, alloc*}`. POD, 비소유 할당자 포인터 (할당자는 호출자 소유).

### 4.2 연산

```cpp
// 라이프사이클
void sb_create(string_builder_t& b, allocator_t& a, int64_t initial_capacity);
//   initial_capacity=0 → 첫 append 까지 할당 안 함 (지연 할당)
void sb_destroy(string_builder_t& b);
//   data != nullptr 일 때만 alloc->free; 필드 제로화. 이중 destroy 안전.

// 추가 (Append)
void sb_append_char(string_builder_t& b, char c);
void sb_append_cstr(string_builder_t& b, const char* s);          // strlen 포함
void sb_append_bytes(string_builder_t& b, const char* p, int64_t n); // 원시 primitive
void sb_append_view(string_builder_t& b, string_t v);             // = append_bytes(v.data, v.len)

// 용량
void    sb_reserve(string_builder_t& b, int64_t total_capacity);  // 보장만, 축소 안 함
int64_t sb_capacity(const string_builder_t& b);

// 검사
int64_t     sb_len(const string_builder_t& b);
bool        sb_is_empty(const string_builder_t& b);
const char* sb_data(const string_builder_t& b);
char*       sb_data(string_builder_t& b);                         // mutable 오버로드
char        sb_at(const string_builder_t& b, int64_t index);      // ingot_assert_ 범위 검사

// 변형 (비추가)
void sb_clear(string_builder_t& b);              // len=0, capacity 유지
void sb_truncate(string_builder_t& b, int64_t new_len);  // ingot_assert_(new_len <= len)
void sb_pop(string_builder_t& b);                // ingot_assert_(len > 0)

// 뷰 추출
string_t sb_to_string(const string_builder_t& b);  // O(1) {b.data, b.len}, 빌더 변경 전까지 유효

// C 상호운용
char* sb_to_cstring(const string_builder_t& b, allocator_t& alloc);
//   len+1 바이트 할당, 복사, NUL 종료, 반환. 호출자가 alloc 으로 해제.
```

### 4.3 성장 전략

기하급수적 2배 — `new_cap = max(required, old_cap * 2)`. 0에서 첫 성장 시 최소 32바이트 할당 (작은 힙 할당 회피). 내부 상수, 향후 튜닝 가능. stb_ds/EASTL 과 동일.

### 4.4 설계 노트

- **지연 할당**: `sb_create(b, a, 0)` 은 할당 없음; 첫 `sb_append_*` 가 초기 할당 트리거. EASTL "빈 컨테이너는 할당 안 함" 과 일치.
- **mutable `sb_data` 오버로드** 로 문서화된 무할당 C 상호운용 탈출구 지원: `sb_data(b)[sb_len(b)] = '\0'; /* b.data 를 cstr 으로 사용 */` — 호출자가 관리하는 임시 NUL. 할당하는 `sb_to_cstring` 이 깨끗한 기본 경로.
- **`sb_resize` (확장+채우기) 없음** — `sb_reserve` 가 용량, `sb_truncate`/`sb_pop` 이 축소. 채우기-확장은 YAGNI.
- **빌더에 반복자 없음** — `sb_to_string(b)` 후 뷰를 반복하거나 `sb_at` 인덱싱. 빌더는 쓰기 지향.

---

## 5. `static_string_builder_t<N>` (스택 소유자, 하드 캡)

### 5.1 레이아웃

```cpp
template <int64_t N>
struct static_string_builder_t {
    static_assert(N > 0, "static_string_builder_t<N>: N must be > 0");
    char    buffer[N];
    int64_t len;
};
// POD 검증: char[N] + int64_t 이므로 구조적으로 trivially copyable & standard layout
static_assert(std::is_trivially_copyable_v<static_string_builder_t<16>> &&
              std::is_standard_layout_v<static_string_builder_t<16>>,
              "static_string_builder_t must be POD");
```

`N` 은 **사용 가능한 바이트 용량** (NUL 예약 없음, Option 2). `len ∈ [0, N]`. 저장소는 구조체 안에 인라인 — 힙 트래픽 없음.

### 5.2 연산

```cpp
// 라이프사이클
template <int64_t N>
void ssb_create(static_string_builder_t<N>& b);   // b.len = 0
// ssb_destroy 없음 — 스택 메모리, 해제 불필요

// 추가 (하드 캡 — 용량 초과 시 ingot_assert_)
template <int64_t N>
void ssb_append_char(static_string_builder_t<N>& b, char c);
template <int64_t N>
void ssb_append_cstr(static_string_builder_t<N>& b, const char* s);
template <int64_t N>
void ssb_append_bytes(static_string_builder_t<N>& b, const char* p, int64_t n);
template <int64_t N>
void ssb_append_view(static_string_builder_t<N>& b, string_t v);

// 용량/검사
template <int64_t N>
int64_t     ssb_capacity(const static_string_builder_t<N>& b);  // 항상 N (컴파일 타임 상수)
template <int64_t N>
int64_t     ssb_len(const static_string_builder_t<N>& b);
template <int64_t N>
bool        ssb_is_empty(const static_string_builder_t<N>& b);
template <int64_t N>
bool        ssb_is_full(const static_string_builder_t<N>& b);   // len >= N
template <int64_t N>
const char* ssb_data(const static_string_builder_t<N>& b);
template <int64_t N>
char*       ssb_data(static_string_builder_t<N>& b);            // mutable 오버로드
template <int64_t N>
char        ssb_at(const static_string_builder_t<N>& b, int64_t index);

// 변형
template <int64_t N>
void ssb_clear(static_string_builder_t<N>& b);
template <int64_t N>
void ssb_truncate(static_string_builder_t<N>& b, int64_t new_len);
template <int64_t N>
void ssb_pop(static_string_builder_t<N>& b);

// 뷰 추출
template <int64_t N>
string_t ssb_to_string(const static_string_builder_t<N>& b);   // O(1) {b.buffer, b.len}

// C 상호운용 (명시적 할당자 — 복사 할당이 필요하므로)
template <int64_t N>
char* ssb_to_cstring(const static_string_builder_t<N>& b, allocator_t& alloc);
//   len+1 바이트 할당, 복사, NUL 종료, 반환. 호출자가 alloc 으로 해제.
```

### 5.3 설계 노트

- **하드 캡 계약은 `sv_push` 를 반영** — 매 append 마다 `ingot_assert_(len + add <= N, ...)`. `static_vector_t` 의 "오버플로우 시 assert" 와 동일. 조용한 잘림 없음, 확장 없음.
- **`ssb_destroy` 없음** — 인라인 버퍼, 해제할 것 없음. `ssb_create` 는 `len` 만 0으로. (값 초기화 `static_string_builder_t<64> b = {};` 도 동작하며 전체 0 채움.)
- **`ssb_is_full`** 로 사전 오버플로우 점검 — `sv_full` 과 대칭. `if (!ssb_is_full(b)) ssb_append_char(b, c);`
- **`ssb_reserve` 없음** — 용량은 컴파일 타임(N)에 고정.
- **`ssb_to_cstring` 은 명시적 할당자** — 타입 자체는 할당자 없이 순수 스택이지만, NUL 종료 복사본을 할당하려면 할당자가 필요. 호출부에서 명시적으로 전달하므로 할당이 보임. 무할당 대안: `ssb_data(b)[ssb_len(b)] = '\0'` (단, `len < N` 일 때만).
- **`int64_t N` 에 대한 함수 템플릿** — 모든 `ssb_*` 는 `template <int64_t N>`. `ssb_append_char(b, 'x')` 처럼 `b` 에서 N 추론.

---

## 6. UTF-8 헬퍼 (옵션)

접두사 `utf8_`. `string_t` (문자열 전체) 또는 raw `const char*` (rune 단위) 에서 동작. rune 타입은 **`char32_t`** (32비트 코드 포인트, "rune" 의미론).

### 6.1 기본 함수

```cpp
// 상수
constexpr char32_t utf8_rune_error = 0xFFFD;  // 치환 문자 (U+FFFD), 무효 시퀀스에 사용

// 검증 (엄격) — 문자열 전체가 유효한 UTF-8 인가?
bool utf8_validate(string_t s);

// 디코딩 (관대한) — 위치 p 에서 1개 rune 디코딩
//   무효 시퀀스: utf8_rune_error 반환, *out_width = 1 (1바이트 건너뜀)
//   이 동작은 반복에 적합 — 항상 전진함
char32_t utf8_decode_rune(const char* p, int64_t remaining, int* out_width);

// 인코딩 — rune 을 1~4 바이트로 out_buf 에 기록
//   out_buf 는 최소 4바이트. 반환 = 기록한 바이트 수 (1~4), 무효 rune 이면 0
int utf8_encode_rune(char32_t rune, char* out_buf);

// rune 개수 (관대한) — 무효 바이트도 1개 rune 으로 카운트
int64_t utf8_rune_count(string_t s);
```

### 6.2 rune 반복 뷰

`string_t` 를 `char32_t` rune 시퀀스로 순회하는 range-for 지원 뷰. POD; operator 들은 자유 함수 (코딩 표준 허용).

```cpp
// 뷰 — string_t 를 rune 시퀀스로 감쌈
struct utf8_rune_view_t {
    string_t source;
};

// 커서 (반복자 역할) — 현재 rune 을 캐시
struct utf8_rune_cursor_t {
    const char* p;         // 다음 rune 의 시작 위치
    const char* end;
    char32_t    current;   // 이미 디코딩된 현재 rune
    int         width;     // current 의 바이트 수
};

// 팩토리
utf8_rune_view_t utf8_runes(string_t s);

// range-for 지원 (ADL)
utf8_rune_cursor_t begin(utf8_rune_view_t v);   // 첫 rune 디코딩하여 캐시
utf8_rune_cursor_t end(utf8_rune_view_t v);     // sentinel: p == end

char32_t            operator*(utf8_rune_cursor_t c);               // 캐시된 current 반환
utf8_rune_cursor_t& operator++(utf8_rune_cursor_t& c);             // p += width, 다음 rune 디코딩
bool                operator!=(utf8_rune_cursor_t a, utf8_rune_cursor_t b); // a.p != b.p
```

**사용 예:**

```cpp
string_t s = str_from_cstr("Hello, 세계");

// range-for — rune 단위 순회
for (char32_t r : utf8_runes(s)) {
    // r 은 char32_t 코드 포인트 — 바이트가 아님
}
```

### 6.3 설계 노트

- **관대한 디코드 + 엄격한 검증 분리.** 디코드는 항상 전진하며 무효 바이트에 `utf8_rune_error` (U+FFFD) 반환 — 반복에 적합 (멈추지 않음). 검증은 "전체 문자열이 유효한가?" 엄격 검사. Odin `decode_rune_in_bytes` / `valid_string` 분리와 동일.
- **`utf8_rune_count` 는 관대한** (디코드와 일관) — 무효 바이트 각각 1 rune 으로 카운트. 엄격 카운트 필요 시 `if (utf8_validate(s)) n = utf8_rune_count(s);`.
- **캐시된 커서** — `begin`/`++` 가 디코드하여 `current`/`width` 저장; `operator*` 는 캐시된 `char32_t` 만 반환. rune 당 1회 디코드.
- **operator 들은 자유 함수** — 타입에 멤버 함수 없음 (코딩 표준 준수; operator 오버로딩은 자유 함수로 허용).
- **POD 검증** — `utf8_rune_view_t` (`string_t` 멤버 1개) 와 `utf8_rune_cursor_t` (스칼라 4개) 모두 trivially copyable + standard layout. `static_assert` 로 검증.
- **`utf8_decode_rune` / `utf8_encode_rune` 은 raw `const char*` / `char*`** — `string_t` 가 아닌, 버퍼 내 *위치* 에서 동작하므로 (반복/빌더 버퍼 기록에 필요).

---

## 7. NUL 종료 정책 (Option 2: 순수 바이트)

### 7.1 정책

**어디서도 암묵적 NUL 없음.** 세 타입 모두 순수 바이트. 예약 +1 용량 없음. 변경 시 NUL 기록부 부담 없음.

### 7.2 근거

원래 "빌더 항상 NUL (Option 1)" 을 고려했으나 두 가지 문제:

1. **비대칭** — 빌더는 NUL 유지, 뷰는 보장 안 함. 같은 타입(`string_t`)이 출처에 따라 NUL 보장이 달라 혼란.
2. **`str_cstr(string_t)` 불가** — 뷰가 NUL 을 보장 안 하므로, 뷰를 C 함수에 넘기려면 빌더로 복사해야 함. 주 타입인 뷰의 C 상호운용 마찰.

**Option 2 (순수 바이트)** 선택: 빌더와 뷰가 동일한 바이트 의미론으로 일관. C 상호운용은 항상 명시적 (보이는 할당). "bytes by default, UTF-8 by convention" 철학과 일치 — Odin/Zig 가 취한 방식.

### 7.3 C 상호운용 경로

```cpp
// (1) 할당하는 복사 — 깨끗한 기본 경로 (sb_to_cstring / ssb_to_cstring)
char* cstr = sb_to_cstring(builder, alloc);
/* C 함수에 cstr 전달 */
alloc.free(cstr, sb_len(builder) + 1);

// (2) 무할당 탈출구 (문서화된 패턴, 함수 아님) — len < capacity 일 때만
sb_data(builder)[sb_len(builder)] = '\0';
const char* cstr = sb_data(builder);   /* C 함수에 전달 — len 은 불변 */
```

정적 빌더는 (1) 에 할당자 명시 전달, (2) 는 `len < N` 일 때만.

---

## 8. 에러 처리 (예외 없음 — 모두 `ingot_assert_`)

| 연산 | assertion |
|---|---|
| `str_at`, `sb_at`, `ssb_at` | `0 <= index < len` |
| `str_slice` | `0 <= begin <= end <= len` |
| `sb_create`, `sb_reserve`, `sb_append_*` (성장 경로) | 할당 실패 시 `data != nullptr` (OOM) |
| `sb_truncate`, `ssb_truncate` | `0 <= new_len <= len` |
| `sb_pop`, `ssb_pop` | `len > 0` |
| `ssb_append_*` | `len + add <= N` (하드 캡 오버플로우) |
| `utf8_decode_rune` | `remaining > 0` (사전 조건; 반복자는 `done()` 점검으로 보장) |
| `utf8_encode_rune` | 사전 조건 없음 — 무효 rune 시 `0` 반환, `out_buf` 4바이트 가정 (문서화) |

OOM, 하드 캡 오버플로우 등 **모든 실패는 `ingot_assert_`** (`sv_create`, `sv_push` 와 일관). 부드러운 실패 경로 없음.

> **암묵적 사전 조건**: 모든 길이/용량/인덱스/바이트 수 인자는 `>= 0` 이어야 한다. 음수 인자는 `ingot_assert_` 로 거부한다 (예: `sb_create(b, a, -1)`, `sb_append_bytes(b, p, -5)`, `sb_reserve(b, -10)`).

---

## 9. 불변 조건 (Invariants)

- **`string_t`**: 비소유. 할당자 없음. 호출자가 `data` 가 뷰보다 오래存活하도록 보장.
- **`string_builder_t`**: `len ∈ [0, capacity]` 소유. `sb_to_string` 으로 얻은 뷰는 다음 변경(append/reserve/destroy) 전까지 유효.
- **`static_string_builder_t<N>`**: `len ∈ [0, N]` 소유 (인라인). 구조체가 살아있는 동안 뷰 유효.
- **NUL 기록부 없음** (Option 2) — 세 타입 모두 일관되게 순수 바이트.
- **POD**: 모든 타입이 `static_assert(is_trivially_copyable && is_standard_layout)` 통과.

---

## 10. 테스팅

코딩 표준 + doctest 스펙(`docs/superpowers/specs/2026-07-04-doctest-integration-design.md`)을 따른다.

### 10.1 테스트 파일 구조 — `doctest_main.cpp` 분리

현재 `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` 이 `test_static_vector.cpp` 최상단에 인라인되어 있다. 스트링 테스트를 **별도 TU**(`test_string.cpp`)로 추가하면 `main()` 이 충돌하므로, doctest 스펙이 예견한 대로 **이 시점에 `tests/doctest_main.cpp` 로 분리**:

```
tests/
├── doctest.h               # (기존)
├── doctest_main.cpp        # 신규 — #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN + #include "doctest.h"
├── test_static_vector.cpp  # 최상단 매크로 제거
├── test_string.cpp         # 신규 — 스트링 타입 테스트
└── CMakeLists.txt          # 두 실행 파일 + doctest_main 링크
```

`tests/CMakeLists.txt`:

```cmake
add_executable(test_static_vector test_static_vector.cpp doctest_main.cpp)
target_link_libraries(test_static_vector PRIVATE ingot)
add_test(NAME test_static_vector COMMAND test_static_vector)

add_executable(test_string test_string.cpp doctest_main.cpp)
target_link_libraries(test_string PRIVATE ingot)
add_test(NAME test_string COMMAND test_string)
```

> **대안**: 스트링 테스트를 `test_static_vector.cpp` 에 추가 (TU 분리 없음). 하지만 파일이 커지고 관심사가 섞이므로 TU 분리를 권장.

### 10.2 테스트 커버리지

`test_string.cpp` 의 `TEST_CASE` 목록 (doctest `CHECK_MESSAGE`/`REQUIRE_MESSAGE` 스타일):

**`string_t`:**
- 생성: `str_from`, `str_from_cstr`, `str_lit` (리터럴 길이 정확성)
- `str_len`, `str_is_empty`, `str_data`, `str_at` (경계 포함)
- `str_equal` (같음/다름, 빈 문자열)
- `str_slice` (정상, 경계 `begin==end`, 전체)
- 바이트 `begin`/`end` range-for

**`string_builder_t`:**
- `sb_create`/`sb_destroy` (이중 destroy 안전)
- `sb_append_char`/`sb_append_cstr`/`sb_append_bytes`/`sb_append_view`
- **성장 (2배)** — 초기 용량 초과 시 재할당, 데이터 보존
- `sb_reserve` (용량 보장, 축소 안 함)
- `sb_clear`, `sb_truncate`, `sb_pop`
- `sb_to_string` 수명 (변경 후 무효)
- `sb_to_cstring` (NUL 종료 검증, 해제)
- 힙 + 아레나 할당자 모두에서 동작

**`static_string_builder_t<N>`:**
- `ssb_create`, `ssb_append_*` (성공)
- **오버플로우 assert** (N 도달 시) — `REQUIRE` 사전 조건 후 `ssb_append_*` 가 abort 하는지는 doctest 로 직접 검증 불가 (abort 포착 불가); 대신 `ssb_is_full` 로 간접 검증
- `ssb_capacity == N`, `ssb_is_full`
- `ssb_to_string`, `ssb_to_cstring`

**`utf8`:**
- `utf8_validate` (유효 ASCII, 다중바이트, 무효 입력)
- `utf8_decode_rune` (ASCII, 2~4바이트, 무효 → U+FFFD + width 1)
- `utf8_encode_rune` (왕복: decode → encode → 바이트 일치)
- `utf8_rune_count` (ASCII, 다중바이트)
- `utf8_rune_view_t` range-for (빈 문자열, 다중바이트, 무효 바이트 포함)
- POD `static_assert` (`TEST_CASE` 내 컴파일 타임 검증)

### 10.3 assert 검증 참고

`ingot_assert_` 는 `std::abort()` 로 프로세스를 종료하므로 doctest 가 직접 포착할 수 없다 (doctest 스펙 §3.2 참고). 오버플로우/OOM 경로의 assert 는:
- 사전 조건 함수(`ssb_is_full` 등)로 간접 검증, 또는
- 별도 프로브(`probe_*.cpp`, 코딩 표준 §6)로 별도 실행.

---

## 11. 파일 구성 — 단일 파일 유지

스트링 타입과 UTF-8 헬퍼는 기존 **`ingot.h` / `ingot.cpp`** 에 추가한다. 별도 파일로 분리하지 않는다.

**근거:**
- 프로젝트의 현재 규칙(단일 라이브러리 헤더/소스)과 일치.
- `ingot.h` 가 약 167줄 → 스트링+UTF-8 추가로 ~450줄. 관리 가능한 범위.
- 분리(`string.h`/`string.cpp`)는 라이브러리가 더 성장했을 때 자연스럽게 도입.

`ingot.h` 구성 순서 (기존 내용 뒤에 추가):

```cpp
namespace ingot {

// ... 기존: allocator_t, heap_allocator_t, arena_allocator_t, static_vector_t ...

// === 스트링 타입 ===
struct string_t { ... };
// str_* 자유 함수

struct string_builder_t { ... };
// sb_* 자유 함수

template <int64_t N>
struct static_string_builder_t { ... };
// ssb_* 함수 템플릿

// === UTF-8 헬퍼 (옵션) ===
constexpr char32_t utf8_rune_error = 0xFFFD;
// utf8_validate, utf8_decode_rune, utf8_encode_rune, utf8_rune_count
// utf8_rune_view_t, utf8_rune_cursor_t, utf8_runes, operator*

} // namespace ingot
```

`ingot.cpp` 에는 비템플릿 구현 추가 (`str_from_cstr`, `str_equal`, `sb_*` 중 헤더에 인라인하지 않는 것들, `utf8_*`). 템플릿(`ssb_*`, `str_lit`)과 짧은 접근자는 헤더에 인라인.

---

## 12. 범위 외 (Out of Scope)

본 설계에서 의도적으로 제외 (YAGNI — 필요 시 별도 스펙):

- **`static_string_builder_t` 의 힙 오버플로우 폴백** — EASTL `fixed_string<N>` 스타일. 하드 캡만 제공.
- **`str_format` / printf 스타일 포매팅** — 로깅 API 가 아직 없음.
- **`str_find`, `str_contains`, `str_starts_with`/`ends_with`, `str_split`, `str_join`** — 탐색/분할/결합.
- **대소문자 변환** (`str_to_lower` 등) — Unicode 변환은 별도 스펙.
- **`str_clone` / `str_dup`** — 뷰의 복사본 할당. (필요 시 `sb_append_view` + `sb_to_cstring` 조합으로 대체 가능.)
- **`str_equal_cstr`** — `const char*` 와의 비교 편의. (`str_equal(v, str_from_cstr(c))` 로 대체.)
- **`sb_resize` (확장+채우기)** — `sb_reserve` + `sb_truncate`/`sb_pop` 으로 커버.
- **UTF-8 정규화(NFC/NFD 등)** — Unicode 정규화는 별도.
- **스트링 인터닝** — Odin `strings.Intern` 스타일. 필요 시 별도.
- **rune 인덱스 기반 랜덤 액세스** (`utf8_rune_at(s, i)`) — O(n) 스캔 필요; 반복으로 커버.

---

## 13. 설계 결정 요약

| 결정 | 선택 | 근거 |
|---|---|---|
| 주 타입 방향 | 뷰-퍼스트 (Odin/Zig) | 비소유 뷰가 주 타입, 빌더가 소유자 — SSO/소멸자/할당자 필드 없음 |
| 소유자 타입 | `string_builder_t` (Odin 스타일) | 비소유 뷰와 명확히 분리된 소유/확장 타입 |
| 네이밍 | `string_t` (뷰, `str_`) + `string_builder_t` (`sb_`) + `static_string_builder_t<N>` (`ssb_`) | 주 타입에 짧은 이름, 빌더는 역할 명시; `sv_` 와 충돌 없음 |
| NUL 종료 | Option 2 — 순수 바이트, 암묵적 NUL 없음 | 빌더/뷰 일관; C 상호운용 명시적; "bytes by default" 철학 일치 |
| 빌더 타입 관계 | Approach 1 — 독립 타입, 접두사 분리 (`sb_`/`ssb_`) | 기존 `static_vector_t` 관례와 일치; 각 타입 자체 완결 |
| UTF-8 | 옵션, 관대한 디코드 + 엄격 검증 | 타입이 강제 안 함 (Odin/Go 방식); `len` 은 바이트 |
| rune 반복 | `utf8_rune_view_t` + range-for | 가독성; 캐시된 커서로 rune 당 1회 디코드 |
| 성장 전략 | 2배 기하급수적, 최소 32바이트 | stb_ds/EASTL 과 일치 |
| 빌더 라이프사이클 | 지연 할당 (`initial_capacity=0` 유효) | EASTL "빈 컨테이너는 할당 안 함" |
| 에러 처리 | 모두 `ingot_assert_` | 예외 없음, 기존 코드와 일관 |
| 파일 구성 | 단일 파일 (`ingot.h`/`ingot.cpp`) | 현재 규칙 유지, 분리는 향후 |
| 테스트 구조 | `doctest_main.cpp` 분리 + `test_string.cpp` 신규 | doctest 스펙이 예견한 TU 분리 시점 |

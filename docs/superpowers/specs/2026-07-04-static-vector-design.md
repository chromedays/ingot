# ingot: Static Vector & Allocator 설계

**날짜:** 2026-07-04
**상태:** 설계 — 구현 대기
**범위:** `pod_containers` 프로젝트의 첫 산출물 — 런타임 디스패치 할당자 인터페이스와 고정 용량 POD 전용 `static_vector_t`.

---

## 1. 동기 및 배경

`pod_containers`는 두 가지 사용 사례를 동시에 타겟하는 C++23 컨테이너 라이브러리이다.

1. **게임 엔진 / 실시간** — 프레임 예산에 민감, 아레나 기반 할당, 예측 가능한 메모리, 숨겨진 힙 확장 금지.
2. **범용 고성능** — 힙 기반이지만 할당을 제어하고 예상치 못한 동작을 원하지 않음.

모든 컨테이너는 **POD 타입**(trivially copyable + standard layout)으로 제한된다. 이 제약은 범용 라이브러리(EASTL, Folly, Unreal)가 짊어져야 하는 복잡도의 상당 부분을 제거한다 — 요소별 `construct`/`destroy` 없음, move-vs-copy 트레이트 디스패치 없음, 요소 연산 중 예외 안전성 없음, relocatability 트레이트 없음. POD bitwise-copy는 항상 안전하므로, 범용 라이브러리가 안전하게 사용할 수 없는 최적화(특히 아레나 in-place `resize`)가 가능해진다.

### 연구 기반

할당자 설계는 기존 시스템 4종에 대한 연구를 바탕으로 한다:

- **EASTL** — 타입 소거(type-erased) 할당자 개념(`rebind` 없음, stateful, 인스턴스 단위). 네 가지 중 가장 깨끗한 할당자 모델. 본 설계는 EASTL의 타입 소거 철학을 취하되, 컴파일 타임 템플릿 파라미터가 아닌 **런타임 가상 디스패치**로 구현한다.
- **stb_ds** — 데이터 전방 헤더 레이아웃, 단일 헤더 배포, "컨테이너는 그냥 포인터" 미학. 미니멀리즘 철학과 단일 헤더류 배포 모델을 채택하되, bare-pointer 컨테이너 모델은 채택하지 않는다(컨테이너별 할당자 및 안정적 컨테이너 식별자와 충돌).
- **Odin** — 암묵적 `context` 기반 할당자 전달; 런타임 fat-pointer 할당자(`{procedure, data}`). 런타임 디스패치 할당자 개념을 검증.
- **Zig** — 명시적 할당자 파라미터 전달; 런타임 fat-pointer 할당자(`{ptr, vtable}`). 역시 런타임 디스패치를 검증. Zig의 `resize`(in-place 전용, bool 반환) vs `remap`(이동 가능) 구분이 인터페이스 축소에 참고가 되었다.

핵심 결정: **런타임 디스패치**(Odin/Zig/EASTL-개념)를 **컴파일 타임 템플릿 디스패치**(EASTL-구현)보다 선택. 하나의 `static_vector_t<T>` 타입이 모든 할당자와 동작하고, 할당자를 런타임에 교체할 수 있으며, API가 더 단순하다(`static_vector_t<int, arena_alloc>`가 아닌 `static_vector_t<int>`).

---

## 2. 할당자 인터페이스

### 2.1 `allocator_t` 추상 인터페이스

```cpp
namespace ingot {

class allocator_t {
public:
    virtual void* alloc(size_t bytes, size_t align) = 0;
    virtual void  free(void* ptr, size_t bytes) = 0;
    virtual bool  resize(void* ptr, size_t old_bytes, size_t new_bytes, size_t align) = 0;
    virtual ~allocator_t() = default;
};

}
```

**세 가지 연산:**

| 메서드 | 목적 | 반환 |
|---|---|---|
| `alloc` | `bytes` 바이트를 `align` 정렬로 할당 | `void*` (실패 시 `nullptr`) |
| `free` | 이전 할당 해제; `bytes`는 원래 요청 크기 | `void` |
| `resize` | 이동 없이 in-place 확장/축소 시도 | `bool` (성공 시 `true`) |

**왜 이 세 가지인가:**

- `alloc` + `free`는 불가결한 핵심이다.
- `resize`는 POD 특화 승리다: 아레나 할당자는 마지막 할당을 bump 포인터를 전진시켜 in-place로 확장할 수 있다 — 복사 제로. 이 연산은 컨테이너가 `alloc`/`free`만으로 **효율적으로 조합할 수 없는** 것이다(새로 `alloc` + `memcpy` + 이전 것 `free`해야 하므로 목적이 무의미해진다). `resize`는 in-place 확장이 불가능할 때 `false`를 반환하며, 호출자는 alloc+copy+free로 폴백한다.
- `remap`(이동을 허용하는 realloc류 확장)은 검토 후 **기각**: `resize` + 컨테이너 측 `alloc`+`memcpy`+`free` 폴백이 주어지면 `remap`은 중복이다. POD의 경우 컨테이너 측 `memcpy`가 이미 최적이므로, 할당자가 내부적으로 수행해도 더 빠르지 않다.
- `free_all`(아레나 리셋)은 인터페이스에서 **기각**: 사용자가 구체 아레나 타입(`arena.reset()`)에 직접 호출하는 아레나 전체 연산이지, 컨테이너가 호출하는 것이 아니다. 컨테이너는 자신의 버퍼를 `free`로 해제한다. `free_all`은 `allocator_t`가 아닌 `arena_allocator_t`에 속한다.

**런타임 디스패치:** 할당자는 가상 인터페이스다. 컨테이너는 `allocator_t*`(비소유)를 보유한다. 할당당 간접 호출 한 번 — 비교적 큰 할당 크기에서는 무시할 수 있으며, 할당은 요소별 핫 패스가 아니다(생성 시, 그리고 미래의 확장 가능 컨테이너에서는 확장 시 발생).

### 2.2 코딩 표준 예외

`allocator_t` 인터페이스는 코딩 표준의 자유 함수 원칙에 대한 **의도적이고 범위가 제한된 예외**다. 다형적 디스패치는 C++에서 멤버 함수(가상 메서드)를 요구하며 — 가상 자유 함수를 선언할 방법은 없다. 이 예외는 `coding_standards.md` 3절에 기록되어 있으며, 할당자 계층(`allocator_t` 및 이를 상속하는 모든 구체 할당자)으로 제한된다. 컨테이너와 기타 타입은 POD 구조체 + 자유 함수 API를 유지한다.

### 2.3 `free`는 원래 바이트 수를 받는다

`free(void* ptr, size_t bytes)`는 원래 할당 크기를 받는다. 이를 통해 풀 할당자는 할당별 메타데이터를 저장하지 않고도 올른 버킷에 블록을 반환할 수 있다. 필요 없는 할당자에는 아무 비용도 없다(파라미터를 무시).

---

## 3. 구체 할당자

첫 버전에 두 할당자를 포함한다. 둘 다 `allocator_t`를 상속하며 멤버 함수 예외 계층의 일부다.

### 3.1 `heap_allocator_t`

```cpp
namespace ingot {

class heap_allocator_t : public allocator_t {
public:
    void* alloc(size_t bytes, size_t align) override;
    void  free(void* ptr, size_t bytes) override;
    bool  resize(void* ptr, size_t old_bytes, size_t new_bytes, size_t align) override;
};

}
```

- `std::malloc`/`std::free`(또는 `alignof(std::max_align_t)` 초과 정렬 시 플랫폼 `aligned_alloc`)를 래핑.
- **무상태** — 데이터 멤버 없음. 단일 기본 생성 인스턴스가 프로세스 전체 힙 할당자로 동작.
- `resize`는 항상 `false` 반환(malloc은 in-place 확장 불가).
- "그냥 동작하게" 하는 할당자. 특정 할당자가 제공되지 않았을 때의 기본 폴백.

### 3.2 `arena_allocator_t`

```cpp
namespace ingot {

class arena_allocator_t : public allocator_t {
    void*  buffer_;
    size_t offset_;
    size_t size_;
public:
    void construct(void* backing_buffer, size_t buffer_size);
    void destroy();
    void reset();

    void* alloc(size_t bytes, size_t align) override;
    void  free(void* ptr, size_t bytes) override;
    bool  resize(void* ptr, size_t old_bytes, size_t new_bytes, size_t align) override;
};

}
```

- 사전 할당된 버퍼에서의 **bump 포인터** 할당자.
- `construct(buffer, size)`: 백킹 버퍼를 저장하고 `offset_ = 0`으로 설정.
- `alloc`: 정렬 패딩과 함께 `offset_`를 전진. 버퍼가 가득 차면 `nullptr` 반환.
- `free`: **no-op** — 아레나는 개별 할당을 회수하지 않는다. (`bytes` 파라미터는 무시됨.)
- `resize`: **할당이 마지막인 경우에만**(즉, `ptr + old_bytes == buffer_ + offset_`) `true` 반환. 이 경우 `offset_`를 전진 또는 후진만 하면 됨 — 복사 제로. 그 외에는 `false`.
- `reset()`: `offset_ = 0`으로 설정. 이것이 아레나 전용 일괄 해제 연산 — `allocator_t` 인터페이스가 아닌 구체 타입에 대해 사용자가 직접 호출.
- `destroy()`: 상태를 정리(사용자가 소유하는 백킹 버퍼는 해제하지 않음).

**`resize`의 승리:** 아레나 기반 미래의 확장 가능 컨테이너는 alloc+copy+free로 폴백하기 전에 `resize`를 먼저 호출할 수 있다. 컨테이너의 버퍼가 아레나에서 가장 최근 할당인 경우(프레임 범위 사용에서 일반적인 상황), 확장은 복사 제로 — 단순히 bump 포인터를 연장하면 된다. 이것이 POD-only가 안전하게 만드는 최적화다: bitwise 이동은 항상 올바르므로, 공간이 있을 때 in-place로 확장하지 않을 이유가 없다.

---

## 4. `static_vector_t`

### 4.1 타입 정의

```cpp
namespace ingot {

template<typename T>
struct static_vector_t {
    static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>,
                  "static_vector_t requires POD types");

    T*           data;
    size_t       count;
    size_t       capacity;
    allocator_t* alloc;

    T&       operator[](size_t index)       { return data[index]; }
    const T& operator[](size_t index) const { return data[index]; }
};

}
```

- **POD 구조체** — 네 개의 데이터 멤버(64비트에서 32바이트), 숨겨진 상태 없음, 구조체 수준에서 trivially copyable.
- **인스턴스 시점 `static_assert`** — 구조체 본문 내에 배치되어 `static_vector_t<non_pod>`가 인스턴스화되는 순간, 어떤 함수도 호출되기 전에 발생. 코딩 표준에 따라 POD 제약을 컴파일 타임에 강제.
- **`operator[]`는 멤버** — 코딩 표준의 operator 오버로딩 예외. 바운드 체크 없음(4.4절 참조).
- **생성자/소멸자 없음** — 생성과 소멸은 명시적 자유 함수(`sv_construct`, `sv_destroy`). RAII 없음. 사용자가 수명을 제어.

### 4.2 자유 함수 API (`sv_` 접두사)

`static_vector_t` 접두사는 코딩 표준의 접두사 축약 규칙에 따라 `sv_`로 축약. 모든 연산은 컨테이너를 첫 인자로 받는 자유 함수다.

```cpp
// 생명주기
template<typename T>
void sv_construct(static_vector_t<T>& v, allocator_t& a, size_t capacity);

template<typename T>
void sv_destroy(static_vector_t<T>& v);

// 변경
template<typename T>
void sv_push(static_vector_t<T>& v, const T& value);

template<typename T>
void sv_pop(static_vector_t<T>& v);

template<typename T>
void sv_clear(static_vector_t<T>& v);

// 반복 (ADL을 통한 range-for 호환)
template<typename T>
T*       sv_begin(static_vector_t<T>& v);
template<typename T>
T*       sv_end(static_vector_t<T>& v);
template<typename T>
const T* sv_begin(const static_vector_t<T>& v);
template<typename T>
const T* sv_end(const static_vector_t<T>& v);

// 접근자
template<typename T>
size_t sv_count(const static_vector_t<T>& v);
template<typename T>
size_t sv_capacity(const static_vector_t<T>& v);
template<typename T>
bool   sv_empty(const static_vector_t<T>& v);
template<typename T>
bool   sv_full(const static_vector_t<T>& v);
```

### 4.3 구현 의미론

**`sv_construct(v, a, capacity)`:**
- `a.alloc(...)`을 통해 `capacity * sizeof(T)` 바이트를 `alignof(T)` 정렬로 할당.
- `v.data`, `v.count = 0`, `v.capacity = capacity`, `v.alloc = &a` 설정.
- 할당 실패 시 `ingot_assert_` (`data == nullptr`).
- **버퍼를 제로화하지 않음** — 할당자에서 온 원시 메모리는 초기화되지 않은 상태로 둠. POD는 기본 생성이 필요 없고, 채우려고 하는데 제로화는 낭비. 제로화된 메모리가 필요하면 제로화 할당자를 사용하거나 별도 `sv_fill_zero` 함수를 사용(범위 외).

**`sv_destroy(v)`:**
- `v.data != nullptr`인 경우 `v.alloc->free(v.data, v.capacity * sizeof(T))` 호출.
- 모든 필드를 null화 (`data = nullptr`, `count = 0`, `capacity = 0`, `alloc = nullptr`).
- 이중 `destroy`는 no-op(null 체크로 보호).
- 요소별 소멸자 호출 없음 — POD는 없음.

**`sv_push(v, value)`:**
- `ingot_assert_(v.count < v.capacity, ...)` — 오버플로우 시 크래시.
- `v.data[v.count] = value; v.count++;` — POD의 bitwise copy.

**`sv_pop(v)`:**
- `ingot_assert_(v.count > 0, ...)` — 언더플로우 시 크래시.
- `v.count--;` — 소멸자 호출 불필요.

**`sv_clear(v)`:**
- `v.count = 0;` — 요소별 해체 없음.

**`sv_begin` / `sv_end`:**
- 각각 `v.data`와 `v.data + v.count` 반환.
- 원시 포인터 반복 — ADL을 통해 `for (auto& x : v) { ... }` 가능.

### 4.4 바운드 체크 정책

- `sv_push` / `sv_pop`: **체크됨** — `ingot_assert_`로 (오버플로우/언더플로우는 논리 버그, 크래시로 크게 실패).
- `operator[]`: **체크 안 됨** — 원시 포인터 역참조, 바운드 체크 없음. 이것이 성능-대-안전성 트레이드오프. 체크된 접근이 나중에 필요하면 별도 `sv_at(v, index)` 함수를 추가 가능(v1 범위 외).

### 4.5 복사 / 이동

- **복사 없음** — 구조체를 복사하면 `data` 포인터가 중복되어 `sv_destroy` 시 이중 해제 발생. 올바른 복사는 새 할당이 필요; 필요하면 `sv_copy(dst, src, allocator)` 함수로 (범위 외).
- **이동 없음** — 구조체는 trivially copyable이므로 "이동"은 필드 복사 후 소스를 null화하는 것. 필요하면 수동으로 가능. v1에는 `sv_move` 함수 없음.

---

## 5. `ingot_assert_` 매크로

```cpp
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
```

- **오버라이드 가능**: `ingot.h` 포함 전에 `ingot_assert_`를 정의하면 커스텀 assert 구현을 사용. 기본값은 `#ifndef`로 건너뜀.
- **접두사 포함**: 매크로는 네임스페이스를 가질 수 없으므로, `ingot_assert_`가 라이브러리 접두사를 달아 충돌을 회피(`assert_` 단독은 충돌).
- **기본값은 `fprintf`/`abort` 직접 사용** — 이것은 크래시/assert 경로이며, 로깅 계층보다 아래. `log_error`를 거치지 않는다. 코딩 표준의 "`std::fprintf(stderr, ...)` 금지" 규칙은 *로깅*을 대상으로 하며, assert-크래시 인프라가 아니다. (사용자가 abort 전 `log_error` 경유를 선호하면 `ingot_assert_`를 오버라이드해서 그렇게 하면 됨.)
- **형식**: `ingot_assert_(cond, "메시지", 인수...)` — 조건 + printf 스타일 메시지.

---

## 6. 파일 구조

두 파일 라이브러리 + 별도 테스트:

```
pod_containers/
├── coding_standards.md
├── CMakeLists.txt
├── ingot.h              // 라이브러리 헤더: 모든 선언 + 템플릿 구현
├── ingot.cpp            // #define INGOT_IMPLEMENTATION + #include "ingot.h"
└── tests/
    └── test_static_vector.cpp
```

### 6.1 `ingot.h`

포함:
- `ingot_assert_` 매크로 (`#ifndef` 오버라이드 가드 포함)
- `namespace ingot { ... }`
- `allocator_t` 추상 클래스
- `heap_allocator_t` 클래스 선언
- `arena_allocator_t` 클래스 선언
- `static_vector_t<T>` 구조체 + `static_assert`
- 모든 `sv_*` 자유 함수 정의 (inline, 템플릿이므로)
- 비템플릿 정의(`heap_allocator_t` 및 `arena_allocator_t` 메서드 본체)는 `#ifdef INGOT_IMPLEMENTATION`으로 보호

### 6.2 `ingot.cpp`

```cpp
#define INGOT_IMPLEMENTATION
#include "ingot.h"
```

비템플릿 메서드 본체를 정확히 한 번 정의. 사용자는 이 파일을 컴파일/링크; 라이브러리는 `ingot.h` + `ingot.cpp`로 배포.

### 6.3 테스트

`tests/test_static_vector.cpp` — 코딩 표준의 `test_*.cpp` 네이밍 관례를 따름. `ingot.cpp`에 링크. CTest로 실행.

---

## 7. 네임스페이스 및 네이밍 요약

| 항목 | 이름 | 관례 |
|---|---|---|
| 라이브러리 네임스페이스 | `ingot` | 짧고, 산업적, 충돌 없음 |
| Assert 매크로 | `ingot_assert_` | 매크로 접두사 + `_` 접미사 |
| 할당자 베이스 | `allocator_t` | `snake_case` + `_t` |
| 힙 할당자 | `heap_allocator_t` | `snake_case` + `_t` |
| 아레나 할당자 | `arena_allocator_t` | `snake_case` + `_t` |
| Static vector | `static_vector_t<T>` | `snake_case` + `_t` |
| Vector 자유 함수 | `sv_*` | 축약 접두사 (static vector → sv) |
| 할당자 메서드 | `alloc`, `free`, `resize`, `construct`, `destroy`, `reset` | 멤버 함수 (할당자 예외) |
| 구현 매크로 | `INGOT_IMPLEMENTATION` | 매크로 스위치용 대문자 snake case |

---

## 8. 사용 예

```cpp
#include "ingot.h"

int main() {
    // 아레나용 백킹 버퍼 (스택 할당)
    alignas(alignof(std::max_align_t)) char buffer[4096];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    {
        ingot::static_vector_t<int> v;
        ingot::sv_construct(v, arena, 64);

        for (int i = 0; i < 10; ++i) {
            ingot::sv_push(v, i * i);
        }

        for (int x : v) {  // ADL sv_begin/sv_end를 통한 range-for
            // x는 0, 1, 4, 9, ...
        }

        ingot::sv_destroy(v);
    }

    arena.reset();  // 아레나 메모리를 한 번에 회수
    arena.destroy();

    return 0;
}
```

---

## 9. 범위 외 (v1)

다음은 본 설계에서 의도적으로 제외되며, 향후 스펙에서 다뤄질 수 있다:

- **확장 가능 `vector_t`** — in-place 아레나 확장에 `resize` 사용, 폴백으로 alloc+memcpy+free. 자연스러운 다음 컨테이너.
- **`hash_map_t`** — flat hash map, POD 키/값.
- **`slot_map_t`** — 안정적 핸들, O(1) 삽입/삭제. 게임 엔진 필수.
- **`ring_buffer_t`** — 스트리밍/이벤트 큐용 순환 버퍼.
- **`sv_at`** — 바운드 체크 요소 접근.
- **`sv_copy` / `sv_move`** — 할당자 인식 복사 및 이동.
- **`sv_fill_zero`** — 생성 후 백킹 버퍼 제로화.
- **추가 할당자** — 풀, 스택, 추적, 가상 메모리 아레나.
- **스레드 안전성** — v1에는 동기화 없음; 할당자와 컨테이너는 단일 스레드.

---

## 10. 설계 결정 요약

| 결정 | 선택 | 근거 |
|---|---|---|
| 할당자 디스패치 | 런타임 (가상) | 하나의 컨테이너 타입, 런타임 교체 가능, 단순한 API |
| 컨테이너 내 할당자 저장 | 비소유 `allocator_t*` | 8바이트, 소유권/수명 결합 없음 |
| 할당자 인터페이스 크기 | 3 메서드 (`alloc`/`free`/`resize`) | `remap`은 컨테이너 측 폴백으로 중복; `free_all`은 아레나 전용 |
| 할당자 코딩 표준 예외 | 예, 할당자 계층으로 제한 | 가상 디스패치는 멤버 함수 요구 |
| 컨테이너 타입 제약 | POD 전용 (`is_trivially_copyable` + `is_standard_layout`) | construct/destroy/move-trait 복잡도 제거; in-place 아레나 `resize` 가능 |
| 컨테이너 구조체 스타일 | POD 구조체 + 자유 함수 | 코딩 표준 준수; `operator[]`만 유일한 멤버 |
| 자유 함수 접두사 | `sv_` (축약) | 코딩 표준의 접두사 축약 규칙 |
| 오버플로우 동작 | `ingot_assert_` (크래시) | 논리 버그는 크게 실패해야; 매크로는 오버라이드 가능 |
| `operator[]` 바운드 체크 | 아니오 | 성능; 체크된 접근(`sv_at`)은 연기 |
| 생성 시 버퍼 제로화 | 아니오 | POD에 낭비; 제로화는 원하면 할당자의 역할 |
| RAII | 아니오 | 명시적 `sv_construct`/`sv_destroy`; 사용자가 수명 제어 |
| 배포 | 두 파일 (`ingot.h` + `ingot.cpp`) | 응집적, 단일 TU가 구현 정의 |
| 네임스페이스 | `ingot` | 짧고, 산업적, 충돌 없음 |
| Assert 매크로 접두사 | `ingot_assert_` | 매크로는 네임스페이스 불가; 접두사로 충돌 회피 |

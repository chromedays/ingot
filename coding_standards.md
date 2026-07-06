# pod_containers 코딩 표준 (Coding Standards)

이 문서는 pod_containers 프로젝트의 코드 작성 표준을 정의합니다.

## 1. 적용 범위
저장소가 직접 소유하는 `*.c`, `*.cc`, `*.cp`, `*.cxx`, `*.cpp`, `*.c++`, `*.C`, `*.h`, `*.hh`, `*.hp`, `*.hxx`, `*.hpp`, `*.h++`, `*.ipp`, `*.tpp`, `*.inl`, `*.m`, `*.mm`, `*.M`

## 2. 포맷
- **들여쓰기**: 4칸 공백, 탭 금지
- **brace 스타일**: K&R brace
- **줄바꿈**: 의미 단위 줄바꿈
- **include 순서**: 구현 파일은 자기 헤더 → 표준 라이브러리 → 서드파티 → 프로젝트 헤더

## 3. 네이밍
- **타입 이름**: `snake_case` (클래스/구조체는 `_t` 접미사 사용)
  ```cpp
  class relative_path_t { /* ... */ };
  ```
- **지역 변수/상수**: `snake_case`
  ```cpp
  const int max_attempts = 3;
  ```
- **함수는 자유 함수(free function)가 원칙**: 클래스/구조체는 데이터 전용(POD-like)이며, 동작은 자유 함수로 구현합니다. 멤버 함수는 문법적으로 불가피한 경우(예: operator 오버로딩)에만 예외적으로 허용합니다.
- **주체를 첫 인자로**: 타입에 대한 동작은 해당 타입의 참조/포인터를 첫 번째 인자로 받습니다.
  ```cpp
  void window_render(window_t& win, float dt);
  float camera_fov(const camera_t& cam);
  ```
- **스코프 접두사**: 특정 타입이나 논리적 모듈/스코프에 속하는 동작의 자유 함수는 해당 이름을 접두사로 시작합니다. 타입 결속인 경우 `_t`를 뺀 이름을 사용합니다.
  ```cpp
  // 타입 결속: camera_t → 접두사 camera_
  void camera_update(camera_t& cam, float dt);
  float camera_fov(const camera_t& cam);
  void camera_reset(camera_t& cam);

  // 모듈/스코프 결속: log_t 타입 없이 로깅 모듈 전반에 속함 → 접두사 log_
  void log_info(const char* fmt, ...);
  void log_warn(const char* fmt, ...);
  void log_error(const char* fmt, ...);
  ```
- **접두사 축약**: 타입 이름이 길어 접두사가 과도하게 길어지는 경우, 타입 이름에서 유추 가능한 축약형을 사용할 수 있습니다. 축약형은 타입별로 일관되게 적용해야 합니다.
  ```cpp
  // static_vector_t → 접두사 sv_ (static vector)
  void sv_create(static_vector_t<int>& v, allocator_t& a, int64_t capacity);
  void sv_push(static_vector_t<int>& v, const int& value);
  int64_t sv_count(const static_vector_t<int>& v);
  ```
- **함수 오버로딩 권장**: 동일한 의미의 연산에 대해 인자 타입만 다른 경우, 이름을 다르게 짓기보다 **함수 오버로딩(function overloading)**을 적극적으로 사용하십시오. API 일관성과 사용성이 향상됩니다. 단, 인자 개수/의미가 다른 경우(예: 원시 바이트 + 길이)는 접미사 구분이 허용됩니다.
  ```cpp
  // BAD: 인자 타입마다 이름을 다르게 지음
  void sb_append_char(string_builder_t& b, char c);
  void sb_append_cstr(string_builder_t& b, const char* s);
  void sb_append_view(string_builder_t& b, string_t v);

  // GOOD: 동일한 이름으로 오버로딩
  void sb_append(string_builder_t& b, char c);
  void sb_append(string_builder_t& b, const char* s);   // strlen 포함
  void sb_append(string_builder_t& b, string_t v);

  // 예외: 인자 개수/의미가 다르면 접미사 허용
  void sb_append_bytes(string_builder_t& b, const char* p, int64_t n);  // 원시 바이트 + 길이
  ```
- **operator 오버로딩 예외**: 문법상 멤버 함수만 허용되는 경우에 한해 멤버 함수를 둘 수 있습니다.
  ```cpp
  struct vec2_t {
      float x, y;
      vec2_t operator+(const vec2_t& rhs) const { /* ... */ }
  };
  ```
- **allocator_t 인터페이스 예외**: 다형적 디스패치가 본질인 `allocator_t` 추상 인터페이스는 멤버 함수(순수 가상 함수)를 허용합니다. 컨테이너는 `allocator_t*`를 비소유 포인터로 저장하며, 구체 할당자(`arena_allocator_t` 등)는 상속으로 구현합니다. 이 예외는 할당자 계층에만 국한됩니다.
  ```cpp
  class allocator_t {
  public:
      virtual void* alloc(int64_t bytes, int64_t align) = 0;
      virtual void  free(void* ptr, int64_t bytes) = 0;
      virtual ~allocator_t() = default;
  };
  ```
- **private 멤버**: `trailing underscore`
  ```cpp
  std::string path_;
  ```
- **매크로**: `snake_case` + `_` 접미사
  ```cpp
  #define default_buffer_size_ 4096
  ```
- **열거형(enum)**: `enum class`가 아닌 일반 `enum`을 사용합니다. 열거형 이름은 `snake_case` + `_t` 접미사를 따르며, 열거값은 열거형 이름에서 `_t`를 제거한 접두사로 시작합니다.
  ```cpp
  enum ply_error_t {
      ply_error_file_open_failed,
      ply_error_invalid_header,
      ply_error_unsupported_format,
  };
  ```

## 4. 로깅
- **로깅 API 이름**: `log_level`, `log_write`, `log_debug`, `log_info`, `log_warn`, `log_error`
- **전역 로깅 함수 사용**: 기존의 `std::fprintf(stderr, ...)` 나 `printf(...)`를 직접 사용하지 마십시오.
  - 새로 작성되거나 수정되는 모든 로깅 출력에는 반드시 `util.h`를 포함하고 `log_info`, `log_warn`, `log_error`, `log_debug` 함수들을 사용하십시오.
  - 포맷팅이 필요할 경우 별도의 `std::format` 호출 없이 바로 인자로 포맷 지정을 수행할 수 있습니다. (예: `log_info("Count: {}", count);`)
- **로그 메시지 언어**: 모든 로그 메시지(문자열 리터럴)는 반드시 **영어**로 작성하십시오. 한국어 로그 메시지는 허용되지 않습니다.
- **출력 안정성**: 로깅 시스템은 매 출력마다 `std::fflush(stderr)`를 자동 호출하므로, 즉시 강제 종료 시에도 로그가 누락되지 않습니다.

```cpp
#include "util.h"

log_info("Base directory initialized: {}", absolute_path::base_directory.string());
```

## 5. 오류 처리
- **C++ 예외 금지**: `throw` 및 `try-catch` 구문을 사용하지 마십시오. 컴파일 플래그로 예외 처리가 꺼져 있습니다.
- **실패 전달**: 실패 가능한 API는 `std::expected` (C++23) 또는 성공 여부 코드/구조체를 사용합니다.
- **즉시 종료**: 즉시 종료가 필요할 경우 `ASSERT` 매크로를 사용합니다.

## 6. 테스트 파일 네이밍
- 공식 CTest 그래프에 등록된 소스만 `test_*.cpp` 이름을 사용합니다.

## 7. 언어 규칙
- **식별자 언어**: 영어
- **코드 주석 언어**: 한국어
- **POD 타입 컴파일 타임 검증**: POD여야 하는 타입은 `static_assert`와 `std::is_trivial`/`std::is_standard_layout` (또는 `std::is_trivially_copyable`)로 컴파일 타임에 반드시 검증합니다.
  ```cpp
  static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>,
                "static_vector_t requires POD types");
  ```
- **Designated Initializers (권장)**: 구조체 초기화 시 C++20 designated initializers 사용을 권장합니다. 각 필드명을 명시하여 가독성과 안전성을 높입니다.
  ```cpp
  camera_t cam = {
      .position = {0.0f, 3.0f, 8.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .fov = 1.0471976f,
  };
  ```

## 8. 리뷰 체크리스트
- include 순서가 규칙을 따르지 않음 (자기 헤더 → 표준 → 서드파티 → 프로젝트)
- 함수/타입/변수 이름이 `snake_case`가 아님
- 클래스/구조체 이름에 `_t` 접미사가 빠짐
- private 멤버가 trailing underscore이 아님 (`m_` prefix 사용)
- `snake_case` + `_` 접미사가 아닌 매크로 이름
- `enum class` 사용 (일반 `enum`이 규칙)
- 열거값에 열거형 이름 접두사가 빠짐 (예: `ply_error_invalid_header`가 아닌 `invalid_header`)
- 자유 함수가 아닌 멤버 함수 (operator 오버로딩 예외 제외)
- 타입에 대한 동작 함수가 주체를 첫 인자로 받지 않음
- 타입/모듈 결속 자유 함수가 해당 이름 접두사(`_t` 제외)로 시작하지 않음
- `std::fprintf(stderr, ...)` / `printf(...)`를 직접 사용한 로깅
- 한국어로 작성된 로그 메시지
- 영어가 아닌 식별자, 한국어가 아닌 코드 주석
- 주석이 코드 동작을 단순 반복함
- 순수 스타일/구조 변경에 동작 수정이 섞여 있음
- `throw`/`try-catch` 구문 사용
- 접두사 축약 사용 시 타입별 축약형이 일관되지 않음
- 동일한 의미의 연산을 오버로딩하지 않고 인자 타입별로 이름을 다르게 지음 (예: `sb_append_char`/`sb_append_cstr`/`sb_append_view` 대신 `sb_append` 오버로딩). 단, 인자 개수/의미가 다른 경우(예: `sb_append_bytes`)는 접미사 허용
- POD 타입에 대해 `static_assert`로 `std::is_trivial`/`std::is_standard_layout` 검증이 빠짐
- 즉시 종료 상황에서 `ingot_assert_` 매크로 대신 다른 방식 사용
- 공식 테스트 소스가 `test_*.cpp`가 아님

# ingot

C++23 POD 컨테이너 라이브러리. 헤더 1개 + 구현 1개(`ingot.h`, `ingot.cpp`)로 구성되며 외부 의존성이 없습니다.

## 사용법 (git submodule + add_subdirectory)

저장소를 서브모듈로 추가한 뒤, 프로젝트의 `CMakeLists.txt`에서 서브디렉토리로 포함하면 네임스페이스 타겟 `ingot::ingot`을 링크하면 끝납니다.

```bash
git submodule add <ingot-repo-url> third_party/ingot
git submodule update --init --recursive
```

```cmake
# 프로젝트의 CMakeLists.txt
add_subdirectory(third_party/ingot)

add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE ingot::ingot)
```

`ingot::ingot`을 링크하면 헤더 경로와 정적 라이브러리가 모두 전이됩니다.

## 옵션

| 변수 | 기본값 | 설명 |
|------|--------|------|
| `INGOT_BUILD_TESTS` | `OFF` | `ingot` 테스트 빌드. `add_subdirectory`로 포함 시 기본적으로 꺼져 있어 컨슈머 빌드를 더럽히지 않습니다. 직접 빌드할 때만 `ON`으로 설정. |

### 직접 빌드/테스트

```bash
cmake -S . -B build -DINGOT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## 포매팅

`sb_format` / `ssb_format` 으로 빌트인 타입(정수, 실수, `bool`, `string_t`, `const char*`, `char`, `char32_t`)과 사용자 정의 타입을 포매팅할 수 있습니다.

```cpp
ingot::string_builder_t b;
ingot::sb_create(b, heap, 0);

// 빌트인 타입
ingot::sb_format(b, "n={} f={} b={} s={}"_str,
                 42, 3.14, true, "hello"_str);

// const char* 도 지원
ingot::sb_format(b, "name: {}"_str, "world");
// 결과: "name: world"
```

### 커스텀 타입 포매터 등록

`format_register_` 매크로로 타입별 포맷을 정의합니다. **네임스페이스 또는 전역 스코프에서만 사용 가능**하며, `sb_format`과 `ssb_format` 양쪽에서 동작합니다.

```cpp
struct vec3 { float x, y, z; };
format_register_(vec3, "({}, {}, {})", _v.x, _v.y, _v.z)

// sb_format
ingot::sb_format(b, "pos: {}"_str, vec3{1, 2, 3});
// 결과: "pos: (1, 2, 3)"

// ssb_format (동일한 등록으로 사용)
ingot::static_string_builder_t<128> sb;
ingot::ssb_create(sb);
ingot::ssb_format(sb, "pos: {}"_str, vec3{1, 2, 3});
// 결과: "pos: (1, 2, 3)"
```

매크로 인자:
- `Type`: 등록할 타입
- `fmt_str`: 포맷 문자열 리터럴
- `...`: placeholder를 채울 값. `_v` 변수로 타입 멤버에 접근

### 빌트인 컨테이너 포매터

아래 타입은 별도 등록 없이 포매팅 가능하며, 요소 타입이 커스텀 포매터를 가지고 있으면 재귀적으로 포매팅됩니다.

| 타입 | 출력 예시 |
|---|---|
| `static_vector_t<T>` | `[1, 2, 3]` |
| `view_t<T>` | `[10, 20, 30]` |
| `string_t` | `hello` |
| `string_builder_t` | `hi [2/64]` |
| `static_string_builder_t<N>` | `abc [3/32]` |
| `utf8_rune_view_t` | `한글` |

```cpp
// 재귀 포매팅
static_vector_t<vec3> points;
sv_push(points, vec3{1, 2, 3});
sv_push(points, vec3{4, 5, 6});

ingot::sb_format(b, "points: {}"_str, points);
// 결과: "points: [(1, 2, 3), (4, 5, 6)]"
```

### 제약 사항
- `format_register_`는 템플릿 특수화를 생성하므로 **네임스페이스 또는 전역 스코프에서만** 사용 가능합니다. 함수 내부에서는 사용할 수 없습니다.
- 동일 타입에 `format_register_`를 중복 사용하면 컴파일 에러가 발생합니다.
- 미등록 타입 사용 시 `static_assert`로 컴파일 에러가 발생합니다.

## 요구사항

- C++23 지원 컴파일러
- CMake 3.16 이상

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

## 요구사항

- C++23 지원 컴파일러
- CMake 3.16 이상

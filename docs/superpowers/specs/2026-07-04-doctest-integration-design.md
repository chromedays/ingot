# ingot: doctest 테스트 프레임워크 도입 설계

**날짜:** 2026-07-04
**상태:** 설계 — 구현 대기
**범위:** `pod_containers` 테스트 인프라를 커스텀 어설션 매크로에서 doctest로 전환.

---

## 1. 동기 및 배경

현재 테스트는 `tests/test_static_vector.cpp` 내에 정의된 커스텀 매크로(`test_assert_`, `test_run_`, `test_done_`)와 수동 카운터(`tests_passed`/`tests_failed`), 그리고 손으로 `main()`을 나열하는 방식으로 동작한다. 이 접근은 의존성이 없다는 장점이 있지만, 다음과 같은 단점이 있다:

- **기능 빈약** — 부분 일치/예외 메시지/서브케이스/타임아웃/벤치마크/필터링 등 테스트 프레임워크 표준 기능 부재.
- **보일러플레이트** — 매 테스트마다 `test_run_(...)` / `test_done_()`을 손으로 써야 하고, `main()`에 함수를 수동 등록해야 함.
- **진단 제한** — 실패 시 파일/줄은 표시되지만, 비교 값 자동 출력이나 이력 등이 없음.

**doctest**는 단일 헤더(amalgamated 배포)로 제공되는 C++ 테스트 프레임워크로, 위 모든 기능을 제공하면서도 빌드 시점 의존성을 거의 만들지 않는다. 본 설계는 doctest를 **vendored amalgamated 단일 헤더**로 도입한다.

### 1.1 통합 방식: Amalgamated Vendoring

세 후보를 검토했다:

| 방식 | 평가 |
|---|---|
| **Amalgamated 단일 헤더 vendoring (채택)** | 빌드 시 네트워크 불필요, submodule 아님, 단순함. 프로젝트의 "네트워크 없이 단일 클론으로 빌드" 철학에 부합. |
| CMake FetchContent | 저장소는 깔끔하지만 configure 시점 다운로드 발생 → 현재 정책과 가장 충돌. |
| 시스템 `find_package(doctest)` | 사용자 사전 설치 필요 → 온보딩 부담. |

핵심 결정: **Amalgamated vendoring**. doctest 공식 배포의 `doctest.h` 단일 파일을 저장소에 직접 커밋한다. 이는 기존 "submodule/`external/` 금지" 규칙을 위반하지 않는다 — submodule도, `external/` 디렉토리도, 빌드 시점 네트워크도 아니다. 정책은 "테스트용 doctest 단일 헤더는 예외적으로 vendoring하여 사용"으로 보완한다.

---

## 2. 파일 구조

```
pod_containers/
├── tests/
│   ├── doctest.h                 # vendored amalgamated 단일 헤더 (신규)
│   ├── CMakeLists.txt            # (변경 없음 — 같은 디렉토리 include로 해석)
│   └── test_static_vector.cpp    # doctest TEST_CASE로 마이그레이션
```

### 2.1 `doctest.h` 배치 — `tests/` 디렉토리

`doctest.h`는 `tests/` 안에 직접 배치한다. 근거:
- doctest는 **테스트 전용** 의존성으로, 라이브러리 본체(`ingot.h`/`ingot.cpp`)는 참조하지 않는다. 테스트 소스와 같은 디렉토리에 두어 소유 관계를 명확히 한다.
- `test_static_vector.cpp`에서 `#include "doctest.h"`로 인용 시, 컴파일러가 소스 파일과 동일 디렉토리에서 헤더를 찾으므로 별도의 `target_include_directories` 불필요 → CMake 변경 최소화.

### 2.2 구현 번역 단위(TU) — 인라인 `main()`

doctest는 정확히 하나의 번역 단위에서 `DOCTEST_CONFIG_IMPLEMENT` 계열 매크로가 정의되어야 한다. 별도 파일(`doctest_main.cpp`) 대신 **`test_static_vector.cpp` 최상단에 인라인**한다:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`는 `main()`을 자동 생성한다 — 기존의 수동 `main()`과 `tests_passed`/`tests_failed` 카운터, 함수 등록 코드가 모두 제거된다.

근거: 현재 테스트 TU가 하나뿐이며, 분리 파일을 두면 향후 다중 TU 구조가 될 때까지 이점이 없다. TU가 늘어나면 그 시점에 `doctest_main.cpp`로 분리하는 것이 자연스럽다(YAGNI).

---

## 3. 마이그레이션 매핑

`tests/test_static_vector.cpp`의 커스텀 패턴을 doctest로 1:1 치환한다.

| 기존 (커스텀) | doctest |
|---|---|
| `static void test_push_pop() { test_run_(...); ...; test_done_(); }` | `TEST_CASE("push/pop") { ... }` |
| `test_assert_(cond, "msg")` | `CHECK_MESSAGE(cond, "msg")` (실패해도 계속) 또는 `REQUIRE_MESSAGE(cond, "msg")` (실패 시 즉시 중단) |
| `test_run_("name")` / `test_done_()` | 제거 — doctest가 케이스 등록과 리포팅을 자동 처리 |
| `tests_passed`, `tests_failed`, `main()` | 제거 — `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`이 자동 생성 |
| `static_assert(std::is_trivially_copyable_v<...>, ...)` | 그대로 유지 (컴파일 타임 검증, 런타임과 무관) |

### 3.1 `CHECK` vs `REQUIRE` 사용 기준

- **`CHECK`** — 한 테스트 안의 독립적인 검증. 하나가 실패해도 나머지 검증을 계속 실행하여 진단 정보를 최대화.
- **`REQUIRE`** — 이후 단계가 전제 조건에 의존할 때. 예: 할당 성공을 확인한 뒤 요소를 검사하는 흐름에서, 할당 실패 시 후속 검증이 무의미하면 `REQUIRE`로 즉시 중단.

기본은 `CHECK`, 전제 조건 실패 시 후속이 무효가 되면 `REQUIRE`를 사용한다.

### 3.2 어설션 매크로 정책 유지

기존 규칙 "**테스트 파일에서 `ingot_assert_` 사용 금지**"은 그대로 유지된다. `ingot_assert_`는 실패 시 `std::abort()`로 전체 프로세스를 종료하므로, doctest가 예외 기반으로 잡을 수 없고 이후 케이스가 실행되지 않는다. 테스트 내 기대 조건 검증은 항상 `CHECK`/`REQUIRE`를 사용한다.

(`ingot_assert_`는 라이브러리 본체의 *프로그래밍 오류 경로* — 오버플로우, 언더플로우, 할당 실패 — 에서 계속 사용된다. 테스트는 이를 직접 호출하지 않고, doctest 검증으로 라이브러리의 올바른 동작을 확인한다.)

---

## 4. CMake

`tests/CMakeLists.txt`는 변경하지 않는다:

```cmake
add_executable(test_static_vector test_static_vector.cpp)
target_link_libraries(test_static_vector PRIVATE ingot)
add_test(NAME test_static_vector COMMAND test_static_vector)
```

`doctest.h`가 `tests/`에 있으므로 `#include "doctest.h"`가 같은 디렉토리에서 해석되고, doctest는 **헤더 전용**(구현은 `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` 인라인)이므로 링크 대상 추가도 불필요하다.

### 4.1 `doctest_discover_tests` 미도입

doctest는 CMake 스크립트(`doctest.cmake`)로 각 `TEST_CASE`를 개별 ctest 엔트리로 등록하는 기능을 제공한다. 그러나 이는 추가 cmake 스크립트 vendoring과 configure 단계 복잡도를 동반한다. 현재 하나의 테스트 바이너리만 있으므로 기존 `add_test` 단일 등록을 유지한다. doctest 자체의 리포터(`--list-test-cases`, 필터링)로 충분하다.

---

## 5. 정책 문서 업데이트

### 5.1 `AGENTS.md` 섹션 1 (Git 및 브랜치)

"외부 의존성 없음" 항목을 보완:

> **외부 의존성 없음**: 이 프로젝트는 `git submodule`나 `external/` 디렉토리를 사용하지 않습니다. 단, **테스트용 doctest 단일 헤더**(`tests/doctest.h`)는 예외적으로 저장소에 직접 vendoring하여 사용합니다. 새 클론/워크트리 후 별도의 의존성 초기화 명령은 필요하지 않습니다.

### 5.2 `AGENTS.md` 섹션 5 (테스트 규칙) — 전면 개정

> ## 5. 테스트 규칙
> - **테스트 프레임워크**: 본 프로젝트는 **doctest**(vendored amalgamated 단일 헤더, `tests/doctest.h`)를 사용합니다. 새 테스트는 `TEST_CASE`와 `CHECK`/`REQUIRE` 매크로로 작성하십시오.
> - **`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`**: `main()` 자동 생성은 현재 `tests/test_static_vector.cpp` 최상단에 인라인되어 있습니다. 테스트 번역 단위가 하나뿐이므로 별도 파일은 없습니다.
> - **프로덕션 assert 매크로 사용 금지**: 테스트 파일(`test_*.cpp`)에서 `ingot_assert_` 매크로를 사용하지 마십시오. 이 매크로는 실패 시 `std::abort()`로 전체 테스트 러너를 즉시 종료시켜 이후 테스트가 실행되지 않습니다. 테스트 내 기대 조건 검증은 doctest의 `CHECK`/`REQUIRE`를 사용하십시오.

### 5.3 `AGENTS.md` 섹션 6 (빌드/테스트 명령)

변경 없음 — 빌드/실행 흐름은 동일. (`ctest --test-dir build`, `./build/tests/test_static_vector`)

---

## 6. doctest 버전 및 출처

- **버전**: doctest 2.4.x (최신 안정). 단일 파일 amalgamated 배포.
- **출처**: doctest 공식 GitHub 릴리스의 `doctest.h`를 다운로드하여 `tests/doctest.h`로 저장.
- **업데이트 정책**: vendoring한 헤더는 수동으로 업데이트한다. 새 버전 필요 시 최신 `doctest.h`로 파일을 교체하고 커밋.

---

## 7. 범위 외

다음은 본 설계에서 의도적으로 제외된다:

- **다중 테스트 바이너리 / 다중 TU** — 현재 단일 TU(`test_static_vector.cpp`). 새 컨테이너 테스트가 추가되면 같은 TU에 `TEST_CASE`를 추가하거나, TU 분리 시점에 `doctest_main.cpp`를 분리.
- **`doctest_discover_tests`** — ctest 통합 고도화. 필요 시 별도 스펙.
- **CI / 커버리지 연동** — 범위 외.
- **doctest의 벤치마크/타임아웃/서브케이스 고급 기능 활용** — 필요한 테스트가 생기면 그때 도입.

---

## 8. 설계 결정 요약

| 결정 | 선택 | 근거 |
|---|---|---|
| 통합 방식 | Amalgamated vendoring | 네트워크 불필요, submodule 아님, 단순 |
| `doctest.h` 위치 | `tests/` | 테스트 전용 의존성, 같은 디렉토리 include로 CMake 변경 최소화 |
| 구현 TU | `test_static_vector.cpp`에 인라인 | 단일 TU이므로 분리 파일 불필요 (YAGNI) |
| 기존 커스텀 매크로 | 전면 제거 | doctest `TEST_CASE`/`CHECK`/`REQUIRE`로 대체 |
| `doctest_discover_tests` | 미도입 | 복잡도 대비 이익 낮음 (단일 바이너리) |
| 정책 업데이트 | 섹션 1 보완 + 섹션 5 전면 개정 | vendoring 예외 명시, doctest 사용 규칙화 |
| `ingot_assert_` 테스트 내 사용 금지 규칙 | 유지 | abort가 doctest 예외 처리와 양립 불가 |

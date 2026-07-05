# doctest 테스트 프레임워크 도입 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `pod_containers` 테스트 인프라를 커스텀 어설션 매크로에서 doctest(vendored amalgamated 단일 헤더)로 전환한다.

**Architecture:** doctest `doctest.h`를 `tests/` 디렉토리에 직접 vendoring하고, `tests/test_static_vector.cpp` 최상단에 `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`을 인라인하여 `main()`을 자동 생성한다. 기존 11개 테스트 함수를 `TEST_CASE` + `CHECK`/`REQUIRE`로 1:1 치환한다. CMake 변경은 없다(같은 디렉토리 include로 해석, 헤더 전용). 정책 문서(`AGENTS.md`)의 섹션 1·5를 doctest 정책에 맞게 보완한다.

**Tech Stack:** C++23, CMake, doctest v2.5.2 (vendored amalgamated single header), CTest.

**참조 스펙:** [docs/superpowers/specs/2026-07-04-doctest-integration-design.md](../specs/2026-07-04-doctest-integration-design.md)

---

## File Structure

| 파일 | 작업 | 책임 |
|---|---|---|
| `tests/doctest.h` | Create | vendored doctest amalgamated 단일 헤더 (테스트 프레임워크) |
| `tests/test_static_vector.cpp` | Modify (전면 재작성) | doctest `TEST_CASE` 기반 테스트 |
| `AGENTS.md` | Modify | 섹션 1 vendoring 예외 보완 + 섹션 5 doctest 규칙 전면 개정 |
| `tests/CMakeLists.txt` | (변경 없음) | 같은 디렉토리 include로 해석되므로 수정 불필요 |

---

## Task 1: doctest amalgamated 헤더 vendoring

**Files:**
- Create: `tests/doctest.h`

- [ ] **Step 1: doctest 헤더 다운로드**

doctest v2.5.2 amalgamated 단일 헤더를 `tests/doctest.h`로 저장한다:

```bash
curl -fsSL -o tests/doctest.h \
  "https://raw.githubusercontent.com/doctest/doctest/v2.5.2/doctest/doctest.h"
```

- [ ] **Step 2: 다운로드 검증**

헤더가 정상적으로 내려받아졌는지 확인한다 (버전 문자열 + 헤더 가드 + 파일 크기):

```bash
grep -m1 "DOCTEST_VERSION" tests/doctest.h
grep -m1 "#ifndef DOCTEST_LIBRARY_INCLUDED" tests/doctest.h
wc -l tests/doctest.h
```

Expected:
- 첫 줄 출력에 `DOCTEST_VERSION ... 2.5.2` 형태의 버전 문자열 포함
- `#ifndef DOCTEST_LIBRARY_INCLUDED` 라인 존재
- 라인 수 약 9000+ 줄 (단일 헤더이므로 큼)

- [ ] **Step 3: 커밋**

```bash
git add tests/doctest.h
git commit -m "Vendor doctest v2.5.2 amalgamated single header

Add doctest.h (amalgamated single-header distribution) under tests/
for the test framework. Pinned to v2.5.2. No submodule, no build-time
network dependency."
```

---

## Task 2: `test_static_vector.cpp`를 doctest로 마이그레이션

**Files:**
- Modify: `tests/test_static_vector.cpp` (전면 재작성)

- [ ] **Step 1: 베이스라인 — 기존 테스트가 통과하는지 확인**

마이그레이션 전 현재 상태에서 빌드/실행하여 통과 기준을 확보한다 (아직 빌드 디렉토리가 없으면 configure 수행):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/tests/test_static_vector
```

Expected: `11 passed, 0 failed` 형태의 출력으로 모든 테스트 통과.

- [ ] **Step 2: 테스트 파일 전면 재작성**

`tests/test_static_vector.cpp`의 내용 전체를 아래로 교체한다. 커스텀 매크로(`test_assert_`/`test_run_`/`test_done_`), 카운터(`tests_passed`/`tests_failed`), 수동 `main()`을 모두 제거하고 doctest로 치환한다:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ingot.h"

TEST_CASE("construct/destroy") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 10);
    CHECK_MESSAGE(v.data != nullptr, "data should not be null");
    CHECK(sv_count(v) == 0);
    CHECK(sv_capacity(v) == 10);
    CHECK(v.alloc == &heap);

    ingot::sv_destroy(v);
    CHECK(v.data == nullptr);
    CHECK(v.count == 0);
    CHECK(v.capacity == 0);
    CHECK(v.alloc == nullptr);
}

TEST_CASE("double destroy is no-op") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);
    ingot::sv_destroy(v);
    ingot::sv_destroy(v); /* should not crash */
    CHECK(v.data == nullptr);
}

TEST_CASE("push/pop") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);

    ingot::sv_push(v, 10);
    CHECK(sv_count(v) == 1);
    CHECK(v[0] == 10);

    ingot::sv_push(v, 20);
    ingot::sv_push(v, 30);
    CHECK(sv_count(v) == 3);
    CHECK(v[0] == 10);
    CHECK(v[1] == 20);
    CHECK(v[2] == 30);

    ingot::sv_pop(v);
    CHECK(sv_count(v) == 2);

    ingot::sv_pop(v);
    ingot::sv_pop(v);
    CHECK(sv_count(v) == 0);

    ingot::sv_destroy(v);
}

TEST_CASE("clear") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 10);
    ingot::sv_push(v, 1);
    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);

    ingot::sv_clear(v);
    CHECK(sv_count(v) == 0);
    CHECK(sv_capacity(v) == 10);
    CHECK(v.data != nullptr);

    ingot::sv_destroy(v);
}

TEST_CASE("empty/full") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 3);
    CHECK(sv_empty(v));
    CHECK(!sv_full(v));

    ingot::sv_push(v, 1);
    CHECK(!sv_empty(v));

    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);
    CHECK(sv_full(v));

    ingot::sv_destroy(v);
}

TEST_CASE("iteration (range-for)") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);
    ingot::sv_push(v, 10);
    ingot::sv_push(v, 20);
    ingot::sv_push(v, 30);

    int expected = 10;
    int count = 0;
    for (int x : v) {
        CHECK(x == expected);
        expected += 10;
        count++;
    }
    CHECK(count == 3);

    ingot::sv_destroy(v);
}

TEST_CASE("arena allocator") {
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    ingot::static_vector_t<int> v;
    ingot::sv_create(v, arena, 64);

    CHECK_MESSAGE(v.data != nullptr, "arena allocation should succeed");

    for (int i = 0; i < 10; ++i) {
        ingot::sv_push(v, i * i);
    }
    CHECK(v[5] == 25);

    ingot::sv_destroy(v);
    arena.reset();
    arena.destroy();
}

TEST_CASE("arena resize") {
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    void* p = arena.alloc(32, 8);
    REQUIRE_MESSAGE(p != nullptr, "initial alloc should succeed");

    bool ok = arena.resize(p, 32, 64, 8);
    CHECK(ok);

    arena.free(p, 64);
    arena.reset();
    arena.destroy();
}

TEST_CASE("arena resize (not last)") {
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    void* p1 = arena.alloc(32, 8);
    void* p2 = arena.alloc(32, 8);
    REQUIRE_MESSAGE(p1 != nullptr && p2 != nullptr, "both allocs should succeed");

    bool ok = arena.resize(p1, 32, 64, 8);
    CHECK(!ok);

    arena.reset();
    arena.destroy();
}

TEST_CASE("heap resize always false") {
    ingot::heap_allocator_t heap;

    void* p = heap.alloc(32, 8);
    REQUIRE_MESSAGE(p != nullptr, "heap alloc should succeed");

    bool ok = heap.resize(p, 32, 64, 8);
    CHECK(!ok);

    heap.free(p, 32);
}

TEST_CASE("POD type constraint") {
    static_assert(std::is_trivially_copyable_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be trivially copyable");
    static_assert(std::is_standard_layout_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be standard layout");
}
```

마이그레이션 매핑 요약 (스펙 섹션 3 준수):
- `test_assert_(cond, "msg")` → `CHECK_MESSAGE(cond, "msg")` (기본) 또는 `REQUIRE_MESSAGE` (전제 조건: arena/heap resize 테스트의 할당 성공 검증 3곳)
- `test_run_(...)` / `test_done_()` → 제거 (doctest 자동 처리)
- `tests_passed`/`tests_failed`/수동 `main()` → 제거 (`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` 자동 생성)
- `static_assert(...)` (POD 검증) → 그대로 유지

- [ ] **Step 3: 빌드**

헤더 의존성이 바뀌었으므로 clean rebuild로 안전하게 간다:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Expected: 컴파일/링크 성공 (에러/경고 없음). doctest 헤더가 test_static_vector.cpp에 컴파일 타임에 포함되어 단일 TU로 빌드됨.

- [ ] **Step 4: 테스트 실행 — 모두 통과 확인**

```bash
./build/tests/test_static_vector
```

Expected: doctest 리포터 출력. 11개 `TEST_CASE` 모두 통과. 종료 코드 0. 예:
```
[doctest] doctest version is "2.5.2"
...
===============================================================================
[doctest] test cases: 11 | 11 passed | 0 failed | 0 skipped
[doctest] assertions: 36 | 36 passed | 0 failed |
[doctest] Status: SUCCESS!
```
(11개 케이스, 런타임 assertion 36개 — 분해된 CHECK/REQUIRE 합계. 핵심 기준은 test cases 11 passed / 0 failed.)

또한 CTest 등록도 확인:
```bash
ctest --test-dir build --output-on-failure
```
Expected: `1/1 Test #N: test_static_vector ... Passed`.

- [ ] **Step 5: 커밋**

```bash
git add tests/test_static_vector.cpp
git commit -m "Migrate test_static_vector to doctest

Replace custom assertion macros (test_assert_/test_run_/test_done_),
manual counters, and hand-written main() with doctest TEST_CASE and
CHECK/REQUIRE. DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN is inlined at the top
to auto-generate main(). All 11 test cases preserved with identical
coverage; preconditions use REQUIRE_MESSAGE, the rest CHECK_MESSAGE/CHECK."
```

---

## Task 3: `AGENTS.md` 정책 업데이트 (섹션 1 · 섹션 5)

**Files:**
- Modify: `AGENTS.md`

- [ ] **Step 1: 섹션 1 — "외부 의존성 없음" 항목에 doctest vendoring 예외 보완**

현재 항목:
```markdown
- **외부 의존성 없음**: 이 프로젝트는 `git submodule`나 `external/` 디렉토리를 사용하지 않습니다. 새 클론/워크트리 후 별도의 의존성 초기화 명령은 필요하지 않습니다.
```

아래로 교체:
```markdown
- **외부 의존성 없음**: 이 프로젝트는 `git submodule`나 `external/` 디렉토리를 사용하지 않습니다. 단, **테스트용 doctest 단일 헤더**(`tests/doctest.h`)는 예외적으로 저장소에 직접 vendoring하여 사용합니다. 새 클론/워크트리 후 별도의 의존성 초기화 명령은 필요하지 않습니다.
```

- [ ] **Step 2: 섹션 5 — 테스트 규칙 전면 개정**

현재 섹션 5 전체:
```markdown
## 5. 테스트 규칙
- **프로덕션 assert 매크로 사용 금지**: 테스트 파일(`test_*.cpp`)에서 `ingot_assert_` 매크로를 사용하지 마십시오. 이 매크로는 실패 시 `std::abort()`로 전체 테스트 러너를 즉시 종료시켜 이후 테스트가 실행되지 않습니다.
- **테스트 전용 assert 사용**: 본 프로젝트는 서드파티 테스트 프레임워크(doctest 등)를 사용하지 않고, `tests/test_static_vector.cpp` 내에 정의된 `test_assert_` 매크로를 사용합니다. 이 매크로는 실패 시 `tests_failed`를 증가시키고 `return`으로 해당 테스트 함수만 종료하므로 나머지 테스트가 계속 실행됩니다. 새 테스트를 추가할 때도 동일한 패턴(`test_assert_`, `test_run_`, `test_done_`)을 따르십시오.
```

아래로 교체:
```markdown
## 5. 테스트 규칙
- **테스트 프레임워크**: 본 프로젝트는 **doctest**(vendored amalgamated 단일 헤더, `tests/doctest.h`)를 사용합니다. 새 테스트는 `TEST_CASE`와 `CHECK`/`REQUIRE` 매크로로 작성하십시오.
- **`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`**: `main()` 자동 생성은 현재 `tests/test_static_vector.cpp` 최상단에 인라인되어 있습니다. 테스트 번역 단위가 하나뿐이므로 별도 파일은 없습니다.
- **프로덕션 assert 매크로 사용 금지**: 테스트 파일(`test_*.cpp`)에서 `ingot_assert_` 매크로를 사용하지 마십시오. 이 매크로는 실패 시 `std::abort()`로 전체 테스트 러너를 즉시 종료시켜 이후 테스트가 실행되지 않습니다. 테스트 내 기대 조건 검증은 doctest의 `CHECK`/`REQUIRE`를 사용하십시오.
```

- [ ] **Step 3: 문서 일관성 확인**

섹션 6(빌드/테스트 명령)은 변경이 필요 없음을 확인한다 — 빌드/실행 흐름이 동일하므로 (`cmake -S . -B build`, `ctest --test-dir build`, `./build/tests/test_static_vector`).

```bash
grep -n "test_static_vector\|doctest\|test_assert_" AGENTS.md
```
Expected: `test_assert_` 검색 결과 없음(제거됨), `doctest`는 섹션 1·5에 등장, `test_static_vector`는 섹션 6에 유지.

- [ ] **Step 4: 커밋**

```bash
git add AGENTS.md
git commit -m "Update AGENTS.md for doctest test framework

Section 1: note tests/doctest.h as an allowed vendored exception to the
no-external-dependencies rule. Section 5: replace the custom test_assert_
macro guidance with doctest TEST_CASE/CHECK/REQUIRE usage; keep the
ingot_assert_ prohibition in tests (abort vs doctest exception model)."
```

---

## 완료 기준 (Definition of Done)

- [ ] `tests/doctest.h` 존재 (doctest v2.5.2, 약 9000+ 줄 단일 헤더)
- [ ] `tests/test_static_vector.cpp` 가 doctest 기반으로 동작, 11개 `TEST_CASE` 모두 통과
- [ ] `rm -rf build` 후 처음부터 빌드/테스트가 성공 (`./build/tests/test_static_vector` 종료 코드 0)
- [ ] `AGENTS.md` 섹션 1·5 가 doctest 정책을 정확히 반영
- [ ] 각 태스크별 커밋 완료 (영어 커밋 메시지)

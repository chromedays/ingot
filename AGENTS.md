# pod_containers 프로젝트 개발 규칙 및 교훈 (Workspace Rules)

이 파일은 pod_containers(라이브러리명 `ingot`) 프로젝트에서 C++23 라이브러리 개발 시 적용해야 하는 개발 교훈과 규칙을 정의합니다.

## 1. Git 및 브랜치 작업 규칙
- **기능 브랜치(Feature Branch) 기반 개발**: 베이스 커밋과의 변경 사항 비교(Diff) 및 검증을 용이하게 하기 위해, `main` 브랜치에서 직접 작업하는 대신 항상 작업 목적에 맞는 **기능 개발 브랜치**를 별도로 생성하여 작업을 시작하고 완료 후 병합(Merge) 또는 PR을 진행하십시오.
- **외부 의존성 없음**: 이 프로젝트는 `git submodule`나 `external/` 디렉토리를 사용하지 않습니다. 단, **테스트용 doctest 단일 헤더**(`tests/doctest.h`)는 예외적으로 저장소에 직접 vendoring하여 사용합니다. 새 클론/워크트리 후 별도의 의존성 초기화 명령은 필요하지 않습니다.
- **Squash Merge**: 기능 브랜치를 `main`에 병합할 때는 반드시 `git merge --squash`를 사용하여 모든 커밋을 하나로 합친 후 커밋하십시오. `main`의 히스토리가 기능 단위로 깔끔하게 유지됩니다. 커밋 메시지의 첫 줄은 기능 전체를 요약하고, 본문에는 자동으로 나열되는 개별 커밋 히스토리를 그대로 유지하십시오.

## 2. 언어 및 소통 규칙
- **한국어 사용 규칙**: 모든 대화 답변, 피드백, 설계 문서(Specs), 구현 계획(Plans)은 가독성과 사용자 편리성을 위해 항상 **한국어**로 작성하십시오.
- **커밋 메시지 언어**: 모든 git 커밋 메시지는 **영어**로 작성하십시오.

## 3. 코딩 표준 문서
- 코드 작성 표준(포맷, 네이밍, 로깅, 오류 처리, 테스트 파일 네이밍 등)은 [coding_standards.md](coding_standards.md)를 따르십시오.

## 4. Assertion 출력 파싱

Assertion 실패 시 `ingot.h`의 `ingot_assert_` 매크로는 stderr에 다음 형식의 두 줄짜리 메시지를 출력한 후 `std::abort()`로 프로세스를 종료한다:

```
ingot assert failed: <포맷된 메시지>
  at <파일경로>:<줄번호>
```

파싱 방법:
1. stderr에서 `ingot assert failed:` 로 시작하는 라인을 찾는다.
2. 해당 라인의 `ingot assert failed: ` 이후 전체를 메시지로 취급한다. (메시지는 `printf` 스타일 포맷이 이미 적용된 상태)
3. 바로 다음 라인에서 `  at ` 접두사 이후를 `<파일경로>:<줄번호>` 로 파싱한다. `:` 를 기준으로 마지막 토큰이 줄번호, 나머지가 파일경로.

예시:
```
ingot assert failed: sv_push: overflow (count=5, capacity=5)
  at ingot.h:98
```

필드 목록:
- `message`: assertion에 전달된 포맷 메시지 (`ingot_assert_`의 가변 인자로 생성)
- `file`: assertion 발생 파일 경로 (매크로가 전개된 위치)
- `line`: assertion 발생 줄번호

## 5. 테스트 규칙
- **테스트 프레임워크**: 본 프로젝트는 **doctest**(vendored amalgamated 단일 헤더, `tests/doctest.h`)를 사용합니다. 새 테스트는 `TEST_CASE`와 `CHECK`/`REQUIRE` 매크로로 작성하십시오.
- **`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`**: `main()` 자동 생성은 현재 `tests/test_static_vector.cpp` 최상단에 인라인되어 있습니다. 테스트 번역 단위가 하나뿐이므로 별도 파일은 없습니다.
- **프로덕션 assert 매크로 사용 금지**: 테스트 파일(`test_*.cpp`)에서 `ingot_assert_` 매크로를 사용하지 마십시오. 이 매크로는 실패 시 `std::abort()`로 전체 테스트 러너를 즉시 종료시켜 이후 테스트가 실행되지 않습니다. 테스트 내 기대 조건 검증은 doctest의 `CHECK`/`REQUIRE`를 사용하십시오.

## 6. 빌드 및 테스트 명령

이 프로젝트는 CMake Presets를 사용하지 않으므로 표준 out-of-source 빌드 명령을 사용합니다.

### 빌드
```bash
# Debug 빌드 (구성 + 컴파일)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Release 빌드 (구성 + 컴파일)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 테스트
```bash
# CTest로 테스트 실행
ctest --test-dir build --output-on-failure

# 테스트 바이너리 직접 실행 (상세 출력)
./build/tests/test_static_vector
```

### 정리
```bash
# 빌드 디렉토리 완전 삭제 후 재구성
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

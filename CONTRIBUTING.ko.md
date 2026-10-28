# dreamor에 기여하기

[English](CONTRIBUTING.md) · **한국어**

관심 가져 주셔서 고맙습니다. dreamor는 한 사람([@Lynth-KR](https://github.com/Lynth-KR))이 관리하고, 버그 신고, 질문,
수정, 아이디어 모두 환영합니다. **이슈, PR, 토론은 영어로 써 주세요.** 이 문서는 먼저 읽을 것, 신고와 제안 방법,
빌드와 테스트, 변경이 들어가는 과정을 다룹니다.

모델링 방법이나 컴파일이 안 되는 이유 같은 질문은 [Discussions](https://github.com/Lynth-KR/dreamor/discussions)로
보내 주세요. 이슈는 버그와 기능 제안을 위한 곳입니다.

## 시작하기 전에

dreamor는 일부러 작게 만든 라이브러리이고, 쓸모의 상당 부분이 하지 않는 일에서 나옵니다. 변경을 제안하기 전에
[철학](docs/ko/philosophy.md)을 읽어 주세요. 모든 변경은 그 원칙에 비추어 판단합니다. 그중 미리 알아 두면 좋은
것들입니다.

- **같은 일을 하는 두 번째 방법은 두지 않습니다.** 파이프 문법이나 멤버 함수의 자유 함수 별칭 같은 것입니다.
- **솔버 API를 감싸지 않습니다.** 제약식, 목적식, 풀기, 쌍대값은 솔버의 표기 그대로 씁니다.
- **성능을 위한 사용자 설정을 두지 않습니다.** 속도는 API에 새 개념을 더하지 않고 안쪽에서 냅니다.
- **범위 밖:** 제약 테이블, 키로 읽는 쌍대값과 reduced cost, 열 생성, 계수 배열 오버로드, 집합의 중복 검사나
  제거, 제약 프로그래밍. 이유는 [하지 않는 것](docs/ko/philosophy.md#하지-않는-것)에 있습니다.

간단한 수정보다 큰 일이라면 먼저 이슈를 열어 설계를 맞춰 주세요. 합칠 수 없는 PR을 만드는 수고를 덜 수 있습니다.

## 버그 신고

버그 신고 양식으로 이슈를 열어 주세요. 양식이 다음을 묻습니다.

- dreamor 버전(또는 커밋), 컴파일러와 버전, 백엔드(Concert 또는 MathOpt)와 버전
- 문제를 보여 주는 가장 작은 코드. 집합 하나, 테이블 하나, 행 몇 개면 대개 충분합니다
- 기대한 결과와 실제 결과. 컴파일 에러라면 앞부분 몇 줄

## 기능 제안

기능 제안 양식을 쓰고, 양식에서 철학을 읽었다고 확인해 주세요. 먼저 풀고 싶은 모델링 문제를 적어 주세요.
가능하면 `sum {(m,i,t) in PRODUCE} ...`처럼 AMPL로 쓰고, 지금은 어떤 코드로 쓰고 있는지도 함께 적어 주세요.
기능은 그것이 어떤 모형을 더 명확하게 만드는지로 판단하므로, 제안하는 API보다 문제 자체가 더 중요합니다.

## 테스트와 개발 환경

테스트는 프레임워크 없이 `tests/run_tests.sh` 하나로 돕니다. 컴파일러는 `CXX`로 고릅니다(기본 `g++`, C++20).

**컴파일러:** GCC 13 이상, Clang 18 이상. CI는 이 범위의 양 끝인 GCC 13과 16, Clang 18과 23만 검증하고 그 사이는
검증하지 않습니다. 플랫폼은 리눅스와 Windows입니다. Windows에서는 clang-cl과 MSVC STL로 빌드되며, CI가 아니라 손으로
확인합니다. MSVC 자체 컴파일러(cl.exe)는 지원하지 않고, Apple Clang은 검증하지 않습니다. 리눅스를 어디서 돌릴지(직접 설치,
WSL2, 컨테이너)는 쓰는 사람이 고르면 됩니다. 최신 컴파일러를 로컬에서 돌려 보려면 Clang은
[apt.llvm.org](https://apt.llvm.org/)에서 설치하고(`CXX=clang++-23 tests/run_tests.sh`), GCC는 공식 컨테이너 이미지를
씁니다(`podman run --rm -v "$PWD":/src -w /src docker.io/library/gcc:16 sh -c 'CXX=g++ tests/run_tests.sh'`,
`docker`도 같은 인자).

| 명령 | 무엇을 | 필요한 것 |
|---|---|---|
| `tests/run_tests.sh` | 집합 계층과 Concert 백엔드 전부(Concert stub 위에서), 컴파일 실패해야 하는 코드가 거부되는지, CPLEX용 코드의 문법 | 없음 |
| `tests/run_tests.sh --cplex [DIR]` | 실제 CPLEX로 풀어서 검증, 예제 실행 | CPLEX Studio (`$CPLEX_STUDIO_DIR`, 기본 `/opt/ibm/ILOG/CPLEX_Studio2212`) |
| `tests/run_tests.sh --mathopt [DIR]` | MathOpt 백엔드를 HiGHS로 풀어서 검증, 예제 실행, 같은 예제를 두 백엔드로 만들어 제약을 대조, 옮겨 온 OR-Tools 예제 실행(SCIP) | OR-Tools C++ 사전 빌드본 (`$ORTOOLS_DIR`, 기본 `/opt/ortools`) |

컴파일되면 안 되는 코드는 `tests/compile_fail/`(Concert)와 `tests/mathopt/compile_fail/`(MathOpt)에 한 파일씩
있습니다. 거부되는지만 보지 않고, 파일 맨 위(SPDX 줄 다음)의 `// expect: <문구>`가 컴파일러의 **첫 에러**(와 거기 딸린 note)에
들어 있는지까지 검사합니다. 사용자가 다음 에러보다 먼저 읽는 것이 의도한 한 문장 메시지인지 보려는 것입니다.
`// expect:`가 없는 케이스는 거부되기만 하면 통과입니다. 같은 폴더의 `control.cc`는 공통 prelude가 정상적으로
컴파일되는지 확인하는 대조군이며, 이것이 실패하면 나머지 케이스는 의미가 없으므로 실패로 처리합니다.

CI(GitHub Actions)는 stub과 MathOpt를 GCC 13과 Clang 18 양쪽으로 돌리고(두 컴파일러 모두에서 `// expect:` 문구를
확인), stub을 `-O3 -DNDEBUG`, AddressSanitizer와 UndefinedBehaviorSanitizer, GCC 16과 Clang 23으로 한 번씩 더 돌리고,
CMake 패키지를 설치해 `find_package`로 가져다 쓰는 것까지 확인합니다(`tests/cmake_consumer`,
`tests/cmake_consumer_mathopt`). CPLEX는 CI에 없으므로 `--cplex`는 로컬에서 돌립니다.

OR-Tools는 설치할 필요 없이 릴리스의 C++ 사전 빌드본을 아무 데나 풀고 그 폴더를 가리키면 됩니다(CMake 불필요).

```bash
mkdir -p ~/opt && cd ~/opt
curl -L https://github.com/google/or-tools/releases/download/v9.15/or-tools_amd64_ubuntu-24.04_cpp_v9.15.6755.tar.gz | tar xz
export ORTOOLS_DIR=~/opt/or-tools_x86_64_Ubuntu-24.04_cpp_v9.15.6755
```

Ubuntu 24.04가 아니면 릴리스 페이지에서 배포판에 맞는 C++ 압축본을 고릅니다.

편집기는 clangd 기준입니다. `python3 tools/gen_compile_commands.py`가 `compile_commands.json`을 만들고(CPLEX와
OR-Tools 경로는 위의 환경 변수로), 저장소의 `.clangd`와 `.vscode/settings.json`이 그것을 읽게 해 둡니다.
`compile_commands.json`은 기계마다 달라서 커밋하지 않습니다.

## 변경 만들기

- **스타일.** `tools/format.sh`가 C++(clang-format 23.1.3)과 Python(ruff 0.17.1)을 포맷하고, 모든 파일 첫 줄의 SPDX
  줄을 넣습니다. `tools/tidy.sh`는 라이브러리, 테스트, 예제에 clang-tidy 23.1.3을 돌리며, 아무것도 보고하지 않아야
  합니다. 도구 버전은 고정되어 있고 스크립트가 `uvx`로 그 버전을 실행하니, [uv](https://docs.astral.sh/uv/)만
  설치하면 됩니다. 에디터의 clang-format 경로를 `tools/clang-format`으로 지정하면 저장할 때도 CI와 같게 포맷됩니다.
  라이브러리 코드는 타입 `PascalCase`, 함수 `camelCase`, 변수 `snake_case`(멤버는 끝에 `_`),
  상수 `kPascalCase`를 씁니다. 예제와 문서는 [철학](docs/ko/philosophy.md#이름-짓기)의 사용자 규칙을 따릅니다.
  개수, 크기, 인덱스는 부호 없는 타입입니다.
- **테스트.** 동작이 바뀌면 테스트도 함께 바꿉니다. 새 컴파일 시점 진단에는 `tests/compile_fail/`에 사례를 더하고,
  `// expect:` 줄에 컴파일러가 가장 먼저 내야 할 문장을 적습니다. GCC 13과 Clang 18 모두에서 성립해야 합니다. 새
  디버그 빌드 계약 검사에는 `tests/contract_fail/`에 사례를 더합니다.
- **코드와 짝을 이루는 문서.** [가이드](docs/ko/guide.md)와 README의 코드는 `tests/test_readme.cpp`가, 헤더 주석의
  예제는 `tests/test_docs.cpp`가 컴파일하므로 함께 바꿉니다. 공개 API가 바뀌면 [API 레퍼런스](docs/ko/reference.md)와
  영어판, 그리고 `skills/dreamor/`의 에이전트 skill도 바꿉니다.
- **성능.** 빨라졌다는 주장에는 측정이 따라야 합니다. catalog가 라이브러리의 두 빌드를 짝지어 비교합니다
  ([bench/README.md](bench/README.md#checking-a-change), 영어). 최적화는 dreamor가 보장하는 성질에 기대야 하고, 몇몇
  셀이 아니라 실험 전체에서, GCC 13으로 빌드해도 Clang 18로 빌드해도 성립해야 합니다. 다른 단계가 크게 얻는다면
  한 단계가 조금 잃을 수는 있지만, 묶음 전체나 솔버를 거친 모델 전체가 느려져서는 안 됩니다.
- **완료**는 기본 테스트가 GCC와 Clang에서 통과하고, MathOpt 백엔드나 공용 코드를 건드렸다면 `--mathopt`도 통과하고,
  `tools/format.sh --check`가 통과하고, `tools/tidy.sh`가 아무것도 보고하지 않는 상태입니다.

개발 규칙 전부는 코딩 에이전트용으로 쓴 [AGENTS.md](AGENTS.md)(영어)에 있습니다. 이 문서로 부족할 때 참고하세요.

## 커밋과 PR

- **커밋 하나에 주제 하나.** 커밋은 한 가지 일을 하고, 무엇을 왜 했는지 적습니다. 제목은 `perf: unite indexes only
  its left set`이나 `docs: ...`처럼 쓰고, 본문에는 근거를, 성능 변경이라면 수치를 적습니다.
- **모든 커밋에 서명해 주세요**(Developer Certificate of Origin). `git commit -s`로 커밋하면
  `Signed-off-by: 이름 <메일>` 줄이 붙고, 이로써 [DCO](https://developercertificate.org/)를 확인하는 것입니다. 즉
  그 변경을 직접 썼거나, 프로젝트의 라이선스(Apache 2.0)로 낼 권리가 있다는 뜻입니다. 서명이 없는 커밋이 든 PR은
  합칠 수 없습니다.
- **CI**는 모든 PR에서 돕니다. 포맷, clang-tidy, GCC 13과 16, Clang 18과 23의 테스트, sanitizer, MathOpt 백엔드,
  CMake 패키지를 확인합니다. CPLEX는 CI에 없으니, Concert 쪽 코드를 건드렸고 CPLEX가 있다면
  `tests/run_tests.sh --cplex`를 돌려 주세요.
- 관리자가 이력이 주제별로 읽히도록 커밋을 합치거나 메시지를 다듬을 수 있습니다.

## 행동 강령

참여하는 모든 사람은 [행동 강령](CODE_OF_CONDUCT.md)(Contributor Covenant 2.1, 영어)을 따릅니다.

<div align="center">

<img src="assets/banner-light.svg#gh-light-mode-only" alt="dreamor — declarative relational extension for algebraic modeling in Operations Research" width="100%">
<img src="assets/banner-dark.svg#gh-dark-mode-only" alt="dreamor — declarative relational extension for algebraic modeling in Operations Research" width="100%">

<br>

<img src="assets/slogan-light.svg#gh-light-mode-only" alt="Model with dreamor. Thrive as a dreamer." width="80%">
<img src="assets/slogan-dark.svg#gh-dark-mode-only" alt="Model with dreamor. Thrive as a dreamer." width="80%">

<br>

[![C++20](https://img.shields.io/badge/C%2B%2B-20-FFD43B?style=flat-square&logo=cplusplus&logoColor=white&labelColor=1D2433)](#빠른-시작)
[![CPLEX Concert](https://img.shields.io/badge/backend-CPLEX%20Concert-FFD43B?style=flat-square&labelColor=1D2433)](#백엔드)
[![OR-Tools MathOpt](https://img.shields.io/badge/backend-OR--Tools%20MathOpt-FFD43B?style=flat-square&labelColor=1D2433)](#백엔드)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-FFD43B?style=flat-square&labelColor=1D2433)](LICENSE)

[빠른 시작](#빠른-시작)&emsp;[집합 만들기](docs/ko/guide.md#집합-만들기--makeset)&emsp;[VarTable](docs/ko/guide.md#변수족--vartable)&emsp;[합](docs/ko/guide.md#합--sumof)&emsp;[OR-Tools 예제](examples/mathopt_samples/README.ko.md)&emsp;[공개 API](docs/ko/reference.md#공개-api-전체-목록)

[English](README.md) · **한국어**

</div>

<br>

C++ 최적화 모형을 위한 얇은 모델링 계층입니다. AMPL의 집합 선언처럼 **인덱스 집합**을 만들고, 그 집합
위에 **변수족**을 한 번에 만들고, 집합을 묶어 **합**을 만듭니다. 제약식과 풀이는 솔버 API를 그대로 씁니다.
백엔드는 CPLEX Concert와 OR-Tools MathOpt(HiGHS, SCIP, Gurobi, Xpress 등)입니다.

```cpp
// set PRODUCE := {m in MACHINES, i in ITEMS[m], t in PERIODS};
const auto sPRODUCE = dreamor::makeSet<MachineCol, ItemCol, PeriodCol>(machines, items_of, periods).collect();

// var Yield {(m,i,t) in PRODUCE} >= 0, <= cap[m] / time[i];
const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};

// subject to Capacity {m in MACHINES, t in PERIODS}: sum {i: (m,i,t) in PRODUCE} time[i] * Yield[m,i,t] <= cap[m];
const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };
const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
for (const auto& [machine, period, rows] : producedByMachinePeriod)
  model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);
```

실제 모형 코드가 어떻게 달라지는지는 [OR-Tools가 직접 제공하는 MathOpt 예제 세 개를 dreamor로 옮긴
것](examples/mathopt_samples/README.ko.md)에서 원본과 나란히 볼 수 있습니다.

---

## 문서

| | |
|---|---|
| [철학](docs/ko/philosophy.md) | dreamor가 지키는 것과 그 이유: 원칙, 모형에서 쓰는 이름, 사용 규칙, 일부러 하지 않는 것 |
| [가이드](docs/ko/guide.md) | 모형이 쓰는 순서대로 각 부분 설명: 열, 엔티티, 집합, 묶기, 변수족, 합, 제약식, 해 읽기 |
| [API 레퍼런스](docs/ko/reference.md) | 공개된 타입과 함수 전부를 표로 |
| [OR-Tools 샘플](examples/mathopt_samples/README.ko.md) | Google의 MathOpt 샘플 세 개를 dreamor로 옮긴 것, 원본과 나란히 |
| [벤치마크](bench/README.md) | dreamor의 속도를 손코딩과 비교해 재는 방법과 결과 (영어) |
| [기여하기](CONTRIBUTING.ko.md) | 버그 신고, 기능 제안, 빌드와 테스트, 변경이 들어가는 과정 |
| [변경 이력](CHANGELOG.md) | 릴리스마다 바뀐 것 (영어) |

## 백엔드

집합 계층(`dreamor/index_set.hpp`)은 어떤 솔버에도 의존하지 않습니다. 변수족(`VarTable`)과 합(`sumOf`)만
솔버의 변수·식 타입을 쓰므로 백엔드마다 네임스페이스와 헤더가 따로 있고, include한 헤더가 곧 의존성입니다.

| 백엔드 | 헤더 | 네임스페이스 | 솔버 |
|---|---|---|---|
| IBM ILOG Concert | `dreamor/concert.hpp` | `dreamor::concert` | CPLEX |
| OR-Tools MathOpt | `dreamor/mathopt.hpp` | `dreamor::mathopt` | HiGHS, SCIP, GLOP, PDLP, CP-SAT(MIP로), GLPK, Gurobi, Xpress 등 |

범위, 변수 종류, 이름, 해 읽기는 그 솔버 API의 표기를 그대로 받습니다(Concert라면 `IloEnv`, `ILOFLOAT`,
`IloCplex`). 이름이 길면 별칭을 둡니다: `namespace concert = dreamor::concert;`. `ValueTable`은 값만 담으므로
백엔드와 무관하게 `dreamor::ValueTable`입니다.

백엔드가 달라도 같은 것: 범위를 숫자나 열 함수로 받는 것, 변수 이름 `<이름>_<키0>_<키1>...`, 집합에 없는 행을
찾으면(`vYield(row)`) `std::out_of_range`, 집합 순서의 순회, `sumOf`의 `where()`/`split()`.

MathOpt에서 달라지는 것은 모두 MathOpt의 표기입니다.

```cpp
namespace math_opt = ::operations_research::math_opt;
math_opt::Model model("lot sizing");
const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vYield{
    model, sPRODUCE, 0, max_possible_qty, /*is_integer=*/false, "Yield"};   // env 대신 Model&, 종류는 is_integer
const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{model, sPRODUCE, 0, 1, true, "Setup"};

for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  const math_opt::LinearExpression totalLoad = dreamor::mathopt::sumOf(rows, machine_usage).build();  // env 없음
  model.AddLinearConstraint(totalLoad <= machine->capacity);
}

const auto result = math_opt::Solve(model, math_opt::SolverType::kHighs);
const auto yieldPlan = vYield.solution(*result);   // 가능해가 없으면 std::logic_error
```

반연속 변수는 MathOpt 모형에 없어서 Concert에만 있습니다. MathOpt의 `sumOf`는 `LinearExpression`으로 모으므로
term이 이차 항(`QuadraticExpression`)을 돌려주면 컴파일되지 않습니다(Concert의 `IloExpr`는 이차 항도 받습니다).

OR-Tools C++ 사전 빌드본으로 빌드할 때는
`OR_PROTO_DLL`을 빈 값으로 정의하고 `libortools`, `libprotobuf`, Abseil 라이브러리를 링크합니다
(`tests/run_tests.sh --mathopt DIR`이 그렇게 합니다).

## 설치 — CMake

헤더만 있는 라이브러리라 빌드할 것은 없고, `dreamor::dreamor` 타깃 하나가 include 경로와 C++20을 넘겨줍니다.
솔버는 쓰는 백엔드에 맞춰 옆에 직접 링크합니다.

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --install build
```

```cmake
find_package(dreamor 0.2 CONFIG REQUIRED)
target_link_libraries(app PRIVATE dreamor::dreamor ortools::ortools)    # dreamor/mathopt.hpp
target_link_libraries(app PRIVATE dreamor::dreamor <CPLEX 타깃>)         # dreamor/concert.hpp
```

OR-Tools 사전 빌드본은 자체 CMake 설정을 갖고 있어서 `find_package(ortools CONFIG)`가 되고, `ortools::ortools`가
include 경로와 정의(`OR_PROTO_DLL`)와 라이브러리를 함께 넘겨줍니다(`CMAKE_PREFIX_PATH`에 OR-Tools 폴더를 추가).
CPLEX는 공식 CMake 설정이 없으므로 쓰던 방식대로 링크합니다. 설치 없이 `add_subdirectory(dreamor)`나
`FetchContent`로 가져와도 같은 `dreamor::dreamor`를 씁니다(이때는 dreamor의 파일을 설치하지 않습니다).

## 빠른 시작

여러 기계, 여러 기간의 로트 사이징입니다. 기계마다 만들 수 있는 품목이 다르고(희소), 기간 사이는 재고로
이어집니다.

```cpp
#include <ilcplex/ilocplex.h>
#include "dreamor/index_set.hpp"
#include "dreamor/concert.hpp"
struct Item { std::string name; double time, hold; std::vector<double> demand; };
struct Machine { std::string name; double capacity, setup; std::vector<const Item*> items; };

// 1) 엔티티 옆에 이름 훅 (변수 이름에 들어갈 토큰)
inline const std::string& keyName(const Item* i) { return i->name; }
inline const std::string& keyName(const Machine* m) { return m->name; }

// 2) 열
using MachineCol = dreamor::Col<const Machine*, "machine">;
using ItemCol    = dreamor::Col<const Item*, "item">;
using PeriodCol  = dreamor::Col<std::size_t, "period">;

// 3) 집합
const auto items_of = [](MachineCol m) { return m->items; };     // 종속 소스: 포인터 범위를 돌려줌
const auto sPERIODS = dreamor::makeSet<PeriodCol>(periods).collect();
const auto sROUTES  = dreamor::makeSet<MachineCol, ItemCol>(machines, items_of).collect();
const auto sPRODUCE = dreamor::makeSet<PeriodCol>(sROUTES, periods).collect();     // 기존 집합 펼치기
const auto sSTOCK   = dreamor::makeSet<ItemCol, PeriodCol>(items, periods).collect();

// 4) 변수
const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{env, sPRODUCE, 0, 1, ILOBOOL, "Setup"};
const dreamor::concert::VarTable<ItemCol, PeriodCol> vStock{env, sSTOCK, 0, IloInfinity, ILOFLOAT, "Stock"};

const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };

// 5) 제약
//    재고 균형: 모든 (품목, 기간)에 대해. 그 기간에 만드는 기계가 없어도 걸려야 하므로 sSTOCK 을 돈다
const auto orderedPeriods = sPERIODS.ordered();
const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();
for (const auto& [i, t] : sSTOCK) {
  IloExpr totalInFlow = dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build();
  if (const auto* prevPeriod = orderedPeriods.prev(t)) totalInFlow += vStock(i, std::get<PeriodCol>(*prevPeriod));
  const double currentDemand = i->demand[t.get()];
  const auto endingStock = vStock(i, t);
  model.add(totalInFlow - currentDemand == endingStock);
  totalInFlow.end();
}
//    기계 용량
const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();
for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  IloExpr totalLoad = dreamor::concert::sumOf(env, rows, machine_usage).build();
  model.add(totalLoad <= machine->capacity);
  totalLoad.end();
}
//    셋업 없이는 생산 없음
for (const auto& [row, yieldVar] : vYield) model.add(yieldVar <= yieldVar.getUB() * vSetup(row));

// 6) 목적
IloExpr holdingCost = dreamor::concert::sumOf(env, sSTOCK, [&](ItemCol i, PeriodCol t) { return i->hold * vStock(i, t); }).build();
IloExpr setupCost = dreamor::concert::sumOf(env, sPRODUCE, [&](MachineCol m, ItemCol i, PeriodCol t) { return m->setup * vSetup(m, i, t); }).build();
IloExpr totalCost = holdingCost + setupCost;
model.add(IloMinimize(env, totalCost));
holdingCost.end();
setupCost.end();
totalCost.end();

// 7) 풀고 해 읽기
IloCplex cplex(model);
cplex.solve();
const auto yieldPlan = vYield.solution(cplex);
for (const auto& [row, qty] : yieldPlan)
  if (qty > 1e-6) print(std::get<MachineCol>(row), std::get<ItemCol>(row), std::get<PeriodCol>(row), qty);
```

전체 예제는 `examples/`에 있습니다. 데이터·열·집합은 솔버와 무관한 `production_planning_sets.hpp` 하나이고,
그 위에 같은 모형을 Concert로 쓴 `production_planning.hpp`(실행: `production_planning.cpp`)와 MathOpt로 쓴
`production_planning_mathopt.hpp`(실행: `production_planning_mathopt.cpp`, HiGHS)가 올라갑니다. 두 모형이
같은 제약을 만드는지는 `tests/mathopt/test_cross_backend.cpp`가 대조합니다. 같은 모형을 `std::map`과
Concert만으로 쓴 비교판은 `production_planning_plain.hpp`입니다.

[`examples/mathopt_samples/`](examples/mathopt_samples/README.ko.md)에는 OR-Tools가 직접 제공하는 MathOpt 예제
세 개(`cocktail_hour`, `time_indexed_scheduling`, `lagrangian_relaxation`)를 dreamor로 옮긴 것과, 각각 무엇이
달라지는지 설명한 글이 있습니다.

## 벤치마크

`bench/`는 dreamor를 같은 모형을 손으로 짠 코드와 비교합니다. 실험계획법으로 두 층에서 잽니다.

- `bench/catalog/`: 조회, 묶기, 집합 연산, slice, 생성, 순서, 엔티티 같은 연산 하나하나를 손코딩(해시 맵, 직접 배열,
  정렬, `std::map`)과 비교합니다. 데이터 모양과 모형이 데이터에 닿는 방식을 요인으로 두고, 솔버 없이 잽니다.
- `bench/scenario/`: 모형 전체를 dreamor와 손코딩으로 끝까지 만들어 솔버가 풀기 시작하는 시점까지 CPLEX와 MathOpt로
  잽니다. 모델링 언어 벤치마크들이 쓰는 p-median과, 이 저장소의 생산계획 예제를 그대로 씁니다. 손 최적화 코드, 그리고
  흔히 짜는 `std::map` 코드(`examples/production_planning_plain.hpp`)와 비교합니다.

```bash
bench/scenario/build.sh
cd bench/scenario && python3 run.py --scenario lotsizing && python3 report.py ../results/<run>
```

설계, 공정성 조건, 읽는 법은 `bench/README.md`에 있습니다. 시간 측정이라 CI에서는 돌리지 않습니다.

## AI 코딩 에이전트

**dreamor로 모형을 짜는 에이전트**에게는 `skills/dreamor/`를 줍니다. Agent Skills 형식(`SKILL.md`)이라
dreamor 코드를 쓰거나 고칠 때만 불려 와서 규칙, 시그니처, 컴파일 에러 문구와 고치는 법을 알려 줍니다. 쓰는
에이전트가 스킬을 읽는 폴더에 통째로 복사합니다(Claude Code라면 프로젝트의 `.claude/skills/dreamor/`나
`~/.claude/skills/dreamor/`, 그 밖의 에이전트는 각자의 스킬 폴더).

**이 저장소를 고치는 에이전트**는 루트의 `AGENTS.md`를 읽습니다. 테스트 방법, 설계 원칙, 진단 메시지를 추가하는
절차, 함께 고쳐야 하는 문서가 들어 있습니다. `CLAUDE.md`는 Claude Code가 같은 파일을 읽도록 `@AGENTS.md` 한 줄로
가져옵니다.

## 감사의 말

dreamor를 만드는 데 큰 도움을 준 Anthropic의 Claude에게 감사드립니다. 구현, 테스트, 도구를 함께 만들었습니다.
설계와 그 뒤의 결정은 저자의 몫입니다.

## 라이선스

[Apache License 2.0](LICENSE)

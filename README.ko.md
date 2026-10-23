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

[빠른 시작](#빠른-시작)&emsp;[집합 만들기](#집합-만들기--makeset)&emsp;[VarTable](#변수족--vartable)&emsp;[합](#합--sumof)&emsp;[OR-Tools 예제](examples/mathopt_samples/README.ko.md)&emsp;[공개 API](#공개-api-전체-목록)

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

## 목차

- [목차](#목차)
- [설계 원칙](#설계-원칙)
- [이름 짓기](#이름-짓기)
- [백엔드](#백엔드)
- [설치 — CMake](#설치--cmake)
- [빠른 시작](#빠른-시작)
- [열(Column)](#열column)
- [집합 만들기 — makeSet](#집합-만들기--makeset)
- [집합에서 집합 파생하기](#집합에서-집합-파생하기)
- [묶기 — groupBy](#묶기--groupby)
- [변수족 — VarTable](#변수족--vartable)
- [합 — sumOf](#합--sumof)
- [제약식](#제약식)
- [해 읽기](#해-읽기)
- [Concert 자원과 end()](#concert-자원과-end)
- [규칙 모음](#규칙-모음)
- [하지 않는 것](#하지-않는-것)
- [공개 API 전체 목록](#공개-api-전체-목록)
- [테스트와 개발 환경](#테스트와-개발-환경)
- [벤치마크](#벤치마크)
- [AI 코딩 에이전트](#ai-코딩-에이전트)
- [감사의 말](#감사의-말)
- [라이선스](#라이선스)

---

## 설계 원칙

- **열은 이름으로 부릅니다.** 튜플의 몇 번째 자리인지가 아니라 `MachineCol`, `PeriodCol` 같은 열 타입으로
  가리킵니다. 술어, 범위 함수, 합의 항(term)은 모두 **자기가 읽는 열을 파라미터로 받는** callable입니다.
- **집합은 사실입니다.** 한 번 만든 집합은 바꿀 수 없습니다(읽기 전용). 다른 집합이 필요하면
  `where`, `select` 같은 파생으로 새로 만듭니다.
- **변수족 하나에 VarTable 하나.** 어떤 변수가 존재하는지는 그 테이블의 집합이 정합니다. 변수가 생기는
  곳은 VarTable 생성자 하나뿐입니다.
- **직관성을 성능과 바꾸지 않습니다.** 성능 개선은 사용자 API에 개념을 추가하지 않는 범위에서만
  (내부 해시, 집합 공유 등) 합니다.
- **한 가지 일에는 한 가지 도구.** 도구마다 맡은 일이 하나이고, 두 도구가 같은 일을 나눠 갖지 않습니다. 같은
  일을 하는 두 번째 문법(파이프 연산자, 자유 함수 별칭 등)을 두지 않는 것도 그래서입니다. 대신 도구를 조합하는
  길은 막지 않습니다. 도구들이 서로 직교하면 다른 조합이 같은 결과에 이르는 일은 자연히 생기고, 어느 쪽으로
  쓰든 맞습니다. `makeSet<>(sORDER_ITEMS, sPROMO)`와 `sORDER_ITEMS.where(sPROMO.membership())`가 같은 행을 내는
  것이 그런 예입니다.

## 이름 짓기

집합과 변수족은 이름 모양을 정해 둡니다. AMPL 모형의 이름 앞에 접두사 하나를 붙인 모양이라, AMPL 주석과
코드가 접두사만 빼면 그대로 대응합니다.

| 대상 | 모양 | 예 | AMPL |
|---|---|---|---|
| 열 타입 | PascalCase + `Col` | `MachineCol`, `PeriodCol` | |
| 집합 (`SetOf`) | `s` + SCREAMING_SNAKE_CASE | `sPRODUCE`, `sPEAK_PRODUCE` | `PRODUCE` |
| 변수족 (`VarTable`) | `v` + PascalCase | `vYield`, `vSetup`, `vStock` | `Yield` |

집합에 접두사를 붙이는 것은 대문자만으로 된 이름이 C++에서 매크로나 상수(`ILOFLOAT`, `ILOBOOL`)와 같은
모양이기 때문입니다.

그 밖의 이름(술어와 term 같은 callable, 식, 묶음, 해 테이블)은 정하지 않습니다. 이 문서의 예제는 열을 받는
callable을 snake_case(`items_of`, `machine_usage`), 나머지를 camelCase(`totalInFlow`, `yieldPlan`)로 씁니다.

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
find_package(dreamor 0.1 CONFIG REQUIRED)
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

## 열(Column)

```cpp
using MachineCol = dreamor::Col<const Machine*, "machine">;   // 값 타입 + 이름
using PeriodCol  = dreamor::Col<std::size_t, "period">;       // 값이 정수여도 됨
```

- `Col`은 감싼 값으로 암묵 변환되고, 포인터라면 `->`와 `*`로 엔티티에 바로 닿습니다
  (`m->capacity`). 값은 `t.get()`으로 꺼냅니다.
- 같은 엔티티 타입에 열이 둘 이상이어도 됩니다. 이름이 다르면 다른 열입니다
  (`FromCol`, `ToCol` 둘 다 `const Node*`).
- **변수 이름에 들어갈 토큰**: 엔티티 포인터 열은 `keyName(const T*)` 함수를 엔티티와 **같은 네임스페이스**에
  두면 그걸 씁니다. `std::format`으로 출력되는 값(정수, 문자열)은 훅이 필요 없습니다.
- **키로 찾는 곳은 열 값만 받습니다.** `vYield(m, i, t)`, `yieldPlan(m, i, t)`, `slice(i, t)`, `prev(t)`,
  `in_routes(m, i)`는 키의 열을 선언 순서대로, 또는 집합의 행 하나를 받습니다. `Col`이 값에서 암묵 변환되더라도
  맨 값(`x(2, 1)`, 맨 포인터)과 중괄호(`slice({i, t})`)는 받지 않고, 무엇을 바꾸라는지 한 문장으로 멈춥니다.
  값이 int인 열 둘이 자리를 바꿔도 조용히 통과하는 일을 막기 위해서입니다. 값에서 시작한다면 `PeriodCol{2}`처럼
  감쌉니다.

## 집합 만들기 — makeSet

```cpp
const auto sNAME = dreamor::makeSet<열...>(소스...).where(술어).where(술어)...collect();
```

`makeSet<A, B, C>`의 열 하나하나에 소스가 하나씩 대응합니다. 소스는 세 종류입니다.

| 소스 | 예 | 뜻 |
|---|---|---|
| 컨테이너 | `machines` | 그 열의 값 전부. 원소가 포인터(`std::vector<const Machine*>`), 엔티티 자체(`std::vector<Machine>`), 스마트 포인터(`std::vector<std::unique_ptr<Machine>>`) 어느 쪽이어도 포인터 열이 됩니다 |
| 종속 소스 | `[](MachineCol m) { return m->items; }` | 앞에서 정해진 열에 따라 달라지는 값들 |
| 기존 집합 | `makeSet<PeriodCol>(sROUTES, periods)` | 그 집합이 가진 열들을 한꺼번에 채움. 같은 열을 가진 집합끼리는 그 열로 조인 |

- **AMPL 대응**
  - `{m in MACHINES, i in ITEMS[m]}` → `makeSet<MachineCol, ItemCol>(machines, items_of).collect()`
  - `{(m,i) in ROUTES, t in PERIODS: open[i,t]}` → `makeSet<PeriodCol>(sROUTES, periods).where(is_open).collect()`
  - `{(m,i) in ROUTES, (i,t) in OPEN}` → `makeSet<>(sROUTES, sOPEN).collect()`
- **같은 열을 가진 집합 소스끼리는 그 열로 조인합니다**(natural join). 나중에 방문하는 집합은 공유 열로 조회하고
  훑지 않으므로 곱을 만들지 않습니다. 비용은 두 집합과 결과의 크기만큼입니다. 한 집합의 열이 다른 집합에 전부
  들어 있으면 그 집합을 거르는 것과 같아서 `sROUTES.where(sOPEN.membership())`과 같은 행이 나옵니다.
  `makeSet<...>`에 적은 열은 항상 새 열이므로, 집합 소스의 열을 거기 다시 적으면 컴파일 오류입니다.
- **집합의 열은 최대 64개입니다.** 집합 source가 채우는 열도 포함입니다. 방문 순서를 계획하는 동안 집합의 열이
  64비트 마스크 하나의 비트이기 때문입니다. 65번째 열은 dreamor 메시지와 함께 컴파일 시점에 막힙니다.
- **술어는 자기가 읽는 열만 받습니다.** `[](ItemCol i, PeriodCol t)`는 i와 t가 정해지는 즉시 평가되고,
  탈락하면 그 아래 루프 전체를 건너뜁니다. 파라미터 순서와 `.where()` 순서는 자유입니다.
- 종속 소스는 엔티티가 아니라 **엔티티의 포인터 범위**(`std::vector<const Item*>`, span, view)를 돌려줘야 합니다.
- 컨테이너 소스는 lvalue여야 합니다(임시 vector 금지).
- 결과 행 순서는 생성 순서이며, 어떤 열이 가장 빨리 변하는지는 고정이 아닙니다. 순서가 의미를 가지면
  `orderBy`로 정렬하세요.
- 집합 타입을 적을 때는 `dreamor::SetOf<MachineCol, ItemCol>`을 씁니다.

## 집합에서 집합 파생하기

집합은 읽기 전용입니다(`begin`/`end`/`size`, range-for와 읽기 전용 알고리즘). 대신 **파생**합니다.
복사는 O(1)이고 행을 공유합니다.

| 멤버 | 하는 일 | AMPL |
|---|---|---|
| `S.where(p, q)` | 술어를 모두 통과한 행 | `{(m,i) in S: p and q}` |
| `S.select<A, B>()` | 열 투영(중복 제거). 모든 열을 다른 순서로 고르면 열 순서 바꾸기 | `setof {(m,i,t) in S} (m,i)` |
| `S.select(expr)` | 행마다 `expr`이 만든 행(중복 제거) | `setof {(i,j) in S} (j,i)` |
| `S.orderBy(key)` | `key(row)`로 안정 정렬 | |
| `S.distinct()` | 첫 등장만 남김 | |
| `S.groupBy<A>()` | 묶기 (다음 절) | 슬라이스 |
| `S.membership()` | O(1) 소속 판정 술어. 다른 집합의 `where`에 넣음 | `(m,i) in S` |
| `S.ordered()` | ord / next / prev / nextCyclic / prevCyclic | ordered set |
| `S.assertUnique<A, B>()` | (디버그 빌드) A,B가 키인지 확인, 아니면 예외. 같은 집합을 돌려줌 | |

| 자유 함수 | |
|---|---|
| `dreamor::unite(A, B)` | A ∪ B |
| `dreamor::intersect(A, B)` | A ∩ B |
| `dreamor::difference(A, B)` | A \ B |
| `dreamor::concat(A, B)` | 이어 붙이기(중복 유지) |

```cpp
const auto sPEAK_PRODUCE  = sPRODUCE.where(is_peak);                  // 수요가 몰리는 기간의 생산 행
const auto sMACHINE_ITEM  = sPRODUCE.select<MachineCol, ItemCol>();  // (기계, 품목)만
const auto in_routes      = sROUTES.membership();
const auto orderedPeriods = sPERIODS.ordered();
if (const auto* prevPeriod = orderedPeriods.prev(t)) { /* 직전 기간 */ }
```

`ordered()`는 중복을 없앤 집합 위의 위치 뷰(`Ordered`)입니다. 위치는 0부터 셉니다. `next`/`prev`/
`nextCyclic`/`prevCyclic`는 원소 뒤에 걸음 수를 받을 수 있고(`next(t, 2)`, 음수 가능) 행의 포인터를 돌려주며,
경계를 넘거나 집합에 없는 원소면 `nullptr`입니다. 나머지는 [공개 API 전체 목록](#공개-api-전체-목록)에 있습니다.

`membership()`이 돌려주는 `Membership`은 술어 객체라서 직접 불러도 됩니다. 열을 따로
(`in_routes(MachineCol{m}, ItemCol{i})`) 넘기거나 행 튜플 하나(`in_routes(row)`)를 넘깁니다.

`select(expr)`는 일반형 `setof`입니다. `expr`은 술어처럼 자기가 읽는 열을 받고, 새 행의 열을 돌려줍니다.
`Col` 하나 또는 그 `std::tuple`입니다. 값은 그 열을 적은 곳에서만 다른 열로 옮겨 갑니다. `ToCol`은 저절로
`FromCol`이 되지 않으므로, 값 타입이 같은 두 열이 모르는 새 자리를 바꾸는 일은 없습니다.

```cpp
// 모든 (i,j)에 대해 (j,i)
const auto sREVERSED = sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; });
const auto sTAILS    = sNODES.select([](NodeCol n) { return FromCol{n}; });  // 노드를 호의 출발점으로
```

`orderBy`의 key는 행 전체를 받으므로 이 라이브러리에서 유일하게 제네릭 람다(`[](const auto& row)`)를
쓸 수 있는 자리입니다.

## 묶기 — groupBy

```cpp
const auto producedByItemPeriod = sPRODUCE.groupBy<ItemCol, PeriodCol>();

for (const auto& [item, period, rows] : producedByItemPeriod) { ... }     // 모든 묶음 (존재하는 키만)
const auto producingRows = producedByItemPeriod.slice(i, t);            // 특정 키 (없으면 빈 묶음)

for (const auto& [item, period, rows] : sPRODUCE.groupBy<ItemCol, PeriodCol>()) { ... }  // 돌기만 하면 이름 없이도 됨
```

- 묶음(`RowSlice`)은 원래 집합의 행을 가리키는 random access 뷰입니다. `producingRows.size()`,
  `producingRows.empty()`, `producingRows[k]`, `producingRows.front()`, `producingRows.back()`, range-for, ranges 알고리즘이 됩니다. 행을 복사하지 않습니다.
- `Grouped`를 돌면 묶음마다 키의 열들과 그 묶음의 행이 차례로 풀립니다(`[item, period, rows]`). 키 튜플이
  통째로 필요하면 이름 하나로 받아 `.key`, `.tuples`를 씁니다.
- 돌기만 할 때는 `groupBy` 결과에 이름을 붙이지 않아도 됩니다. range-for가 범위 식의 결과를 루프 끝까지 살려
  두기 때문입니다. `slice()`, `at()`, `keyAt()`처럼 묶음을 빌려 가는 접근은 이름을 붙인 `Grouped`에서만 됩니다
  (임시 객체에서는 컴파일 오류).
- 묶음 순서는 키의 첫 등장 순서, 묶음 안 순서는 원래 집합 순서입니다.
- **`Grouped`를 돌면 존재하는 키만 돕니다.** 빠른 시작의 재고 균형처럼 드라이버 집합(`sSTOCK`)에는 있는데 이
  집합(`sPRODUCE`)에는 없는 키가 있다면(그 기간에 아무도 만들지 않는 품목), 그 제약족은 `Grouped`를 도는 게 아니라
  **드라이버 집합을 돌면서 `slice()`**로 써야 합니다. 그래야 빈 키에도 제약이 걸립니다.

## 변수족 — VarTable

```cpp
const dreamor::concert::VarTable<키 열...> vName{env, 집합, lb, ub, type, "Name"};
```

- **한 번에, 집합에서** 만듭니다. 만든 뒤 구조(어떤 키가 있는지, 순서, 키와 변수의 대응)는 바뀌지 않습니다.
  `add`나 `create` 같은 건 없습니다.
- `lb`, `ub`는 각각 **숫자 또는 열 함수**입니다. 함수는 `where`의 술어와 같은 규칙을 따릅니다.
  ```cpp
  const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
  const dreamor::concert::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty, ILOFLOAT, "Yield"};
  ```
- 생략하면 `lb = 0`, `ub = IloInfinity`, `ILOFLOAT`, 이름 없음.
- 반연속 변수: `dreamor::concert::VarTable<...> vBatch{dreamor::concert::semiContinuous, env, sBATCH, sclb, ub, ILOFLOAT, "Batch"};`
- 이름은 `이름_키0_키1...` (`Yield_M0_I3_2`). 키 토큰은 `keyName()` 훅에서 옵니다. `nullptr`이면 이름 없음.
- 집합에 같은 행이 두 번 있으면 생성자가 `std::invalid_argument`를 던집니다(`S.distinct()`로 만드세요).
- VarTable은 생성자에서 집합을 받으므로, 모델을 구조체로 묶을 때는 집합을 기반 클래스에 두고 VarTable을
  파생 클래스의 멤버로 둡니다(기반 클래스가 먼저 만들어집니다). 예제의 `Sets` / `Model`이 그 모양입니다.

| 쓰기 | |
|---|---|
| `vYield(m, i, t)` / `vYield(row)` | 변수. 집합에 없는 키면 `std::out_of_range` |
| `vYield.contains(...)` | 키가 있는지 |
| `vYield.size()` | 변수 수 |
| `vYield.keys()` | 테이블의 집합 |
| `for (const auto& [row, yieldVar] : vYield)` | 집합 순서대로 (행, 변수) — 모델에 들어가는 열 순서 |
| `vYield.solution(cplex)` | 해 읽기 (아래) |

**범위를 나중에 조이기**는 새 API 없이 집합으로 고르고 솔버 API로 고칩니다(아래는 Concert, MathOpt라면
`model.set_upper_bound(vYield(row), 0)`).

```cpp
const auto in_maintenance = [](MachineCol m, PeriodCol t) { return m->name == "M0" && t.get() == 2; };
for (const auto& row : vYield.keys().where(in_maintenance)) vYield(row).setUB(0);
```

추출(`IloCplex cplex(model)`) 전에 하면 변경 통지 비용이 없습니다.

**변수족 하나당 테이블 하나.** 부분집합이나 묶음의 변수는 `vYield(row)`로 찾습니다. 부분집합 위에
테이블을 또 만들면(`VarTable{env, sPRODUCE.where(p), ...}`) 그 행마다 **별개의 변수**가 하나 더 생깁니다.
라이브러리는 이것을 "같은 집합 위의 서로 다른 변수족"(예제의 `vYield`와 `vSetup`)과 구별할 수 없어서 검사로 막지
않습니다.

VarTable은 복사할 수 없고 이동만 됩니다(복사하면 같은 변수를 두 이름이 가리키게 되므로).

## 합 — sumOf

```cpp
dreamor::concert::sumOf(env, 행들, term).build()                       // IloExpr
dreamor::concert::sumOf(env, 행들, term).where(p, q).build()           // p AND q 인 행만
dreamor::concert::sumOf(env, 행들, term).split(p1, p2).build()         // std::array<IloExpr, 2>, 한 번 돌며 여러 합
dreamor::concert::sumOf<MachineCol>(env, machines, term).build()       // 일반 컨테이너를 한 열 집합처럼
```

- `행들`은 집합 또는 묶음(`RowSlice`)입니다.
- `build()`는 일부러 따로 둡니다. `sumOf(...)`는 `build()` 전까지 빌더라서 `where()`, `split()`을 이어 붙일 수
  있고, `build()`를 빠뜨린 채 비교·대입·`+=`·목적식에 쓰면 `build()`를 붙이라는 한 문장으로 컴파일이 멈춥니다.
- term은 읽는 열만 받고 그 행의 항을 돌려줍니다. 계수는 엔티티에서 계산합니다(계수 배열을 따로 두지 않음).
  ```cpp
  const auto setup_cost = [&](MachineCol m, ItemCol i, PeriodCol t) { return m->setup * vSetup(m, i, t); };
  ```
- 계수 없이 변수만 더할 때는 **테이블을 그대로 term으로** 넘깁니다. 조회만 하는 람다가 필요 없습니다.
  ```cpp
  dreamor::concert::sumOf(env, producedByItemPeriod.slice(i, t), vYield).build()   // sum {m: (m,i,t) in PRODUCE} Yield[m,i,t]
  ```
  테이블의 키 열로 행에서 열을 골라 넣으므로, 행이 테이블보다 열을 더 가져도 됩니다.
- `split`은 term을 행마다 한 번만 계산합니다.
- AMPL: `sum {(m,i,t) in PRODUCE} setup[m] * Setup[m,i,t]` → `sumOf(env, sPRODUCE, setup_cost)`

Concert에서 `build()`가 돌려주는 `IloExpr`의 수명은 받은 쪽 책임입니다(아래 [Concert 자원과 end()](#concert-자원과-end)).
MathOpt의 `LinearExpression`은 보통의 값이라 신경 쓸 것이 없습니다.

## 제약식

제약식은 **솔버 API를 그대로** 씁니다. 아래는 Concert이고, MathOpt라면 `model.AddLinearConstraint(...)`입니다.
라이브러리에 제약 테이블은 없습니다(이유는 [하지 않는 것](#하지-않는-것)).

```cpp
// 집합 위의 제약족: 묶음마다 하나
for (const auto& [machine, period, rows] : producedByMachinePeriod)
  model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);

// 단 하나뿐인 전역 제약: 이름이 필요하면 setName
IloRange setupBudget = (dreamor::concert::sumOf(env, sPRODUCE, setup_cost).build() <= B);
setupBudget.setName("SetupBudget");
model.add(setupBudget);
```

- Concert에는 `IloExpr(숫자)` 생성자가 없습니다. 숫자는 그대로 비교하고(`expr <= 1`, `totalInFlow - currentDemand == endingStock`),
  변수는 감싸지 말고 그대로 씁니다(`yieldVar <= ub * vSetup(row)`).
- 한 키에 제약이 둘(하한·상한)이면 루프 안에서 둘 다 추가합니다.
- 일부 키에만 제약이 있으면 드라이버 집합을 먼저 `where`로 거릅니다.
- 이름은 필요할 때 `setName`으로 붙입니다. LP 내보내기나 충돌 분석(infeasible 원인 찾기) 결과를 읽을 때 유용합니다.

## 해 읽기

```cpp
const auto yieldPlan = vYield.solution(cplex);   // getValues 한 번으로 테이블 전체
yieldPlan(m, i, t);                          // 키로 값
for (const auto& [row, qty] : yieldPlan) { ... } // 집합 순서대로
```

- 결과(`ValueTable`)는 double만 들고 있어서 `cplex`나 `env`가 끝난 뒤에도 쓸 수 있습니다. 백엔드와 무관한
  같은 타입입니다.
- 집합에 없는 키로 찾으면(`yieldPlan(row)`) 0 대신 `std::out_of_range`를 던집니다(0이면 "값이 0인 변수"와
  구별되지 않으므로).
- Concert의 `solution()`은 `getEnv()`와 `getValues(IloNumArray, IloNumVarArray)`가 있는 알고리즘이면 받습니다.
  안에서 만든 임시 배열은 스스로 정리합니다.
- MathOpt의 `solution()`은 `math_opt::Solve`가 돌려준 `SolveResult`를 받습니다(`vYield.solution(*result)`).
  가능해가 없으면 `std::logic_error`를 던집니다. MathOpt 자체는 이 경우 프로세스를 멈추기(CHECK 실패) 때문입니다.

## Concert 자원과 end()

Concert 객체는 RAII가 아닙니다. 이 라이브러리의 방침은 다음과 같습니다.

- **마지막에 `env.end()` 한 번.** 이것이 모든 Concert 객체를 정리합니다. 두 번 부르면 안 됩니다.
- **VarTable은 소멸할 때 Concert를 건드리지 않습니다.** 변수는 모델이 쓰는 객체이고, 보통 `env.end()`가
  VarTable 소멸보다 먼저 일어나기 때문입니다.
- **변수를 `end()`하지 마세요.** 변수 `end()`는 그 변수를 모델과 모든 제약에서 빼고 지웁니다. VarTable에서
  꺼낸 변수나, 그것들을 담은 배열에 `endElements()`를 부르는 것도 같은 일입니다.
  (배열 자체의 `end()`는 배열만 지우고 변수는 남깁니다.)
- **임시 `IloExpr`는 만든 쪽(받은 쪽) 책임입니다.** 제약을 만든 뒤에는 식을 `end()`해도 안전합니다
  (식은 제약에 깊은 복사로 들어감). 제약이 수백만 개인 모델에서는 메모리 차이가 납니다.

MathOpt에는 이 절이 해당하지 않습니다. `math_opt::Model`과 `LinearExpression`은 보통의 C++ 객체라 범위를
벗어나면 정리되고, `end()` 같은 호출이 없습니다. 변수(`math_opt::Variable`)는 모형을 가리키는 값이므로 모형이
살아 있는 동안만 씁니다.

## 규칙 모음

1. callable(술어, 종속 소스, 범위 함수, 합의 term)은 **열 타입**을 파라미터로 받습니다.
   `[](const Item* i)`나 `[](auto i)`는 컴파일 오류입니다. 예외는 `orderBy`의 key(행 전체를 받음)뿐입니다.
2. 종속 소스는 엔티티의 **포인터** 범위를 돌려줍니다.
3. 집합은 읽기 전용입니다. 바꾸지 말고 파생하세요.
4. 변수족 하나당 VarTable 하나, 그 족의 **전체 집합** 위에. 부분은 `vYield(row)`.
5. 드라이버 집합의 모든 키에 제약이 필요하면 `Grouped`를 돌지 말고 드라이버를 돌며 `slice()`.
6. 같은 행이 두 번 있는 집합은 합에서 두 번 셉니다. 의심되면 `assertUnique()`.
7. 변수를 `end()`하지 않습니다. `env.end()`는 마지막에 한 번.
8. `Grouped`의 `slice()`/`at()`/`keyAt()`, `Ordered`의 조회는 이름을 붙인 객체에서 씁니다. 묶음은 그 객체가 살아 있는 동안만
   유효합니다. 돌기만 할 때는 `for (... : S.groupBy<C>())`처럼 임시 객체도 됩니다.
9. `operator()`가 템플릿이라 파라미터로 열을 알 수 없는 함수 객체는 `dreamor::PredicateColumns`를 특수화해서
   읽는 열을 선언하면 술어로 쓸 수 있습니다.
   ```cpp
   template <> struct dreamor::PredicateColumns<MyFilter> { using type = std::tuple<MachineCol, PeriodCol>; };
   ```
10. 제네릭 코드(`[](const auto& sANY)`)에서 `sANY.select<A>()`처럼 열을 템플릿 인자로 받는 멤버를 부르면
   `sANY.template select<A>()`가 필요합니다. 집합을 구체 타입으로 다루는 권장 방식에서는 생기지 않습니다.
11. 키로 찾을 때는 열 값을 선언 순서대로 넘깁니다(`vYield(m, i, t)`, `slice(i, t)`, `prev(t)`). 중괄호와 맨 값은
   받지 않습니다([열](#열column) 참고).

## 하지 않는 것

| 하지 않는 것 | 이유 / 대안 |
|---|---|
| 제약 테이블(키별 제약 보관, 키 기반 제약 이름) | 제약은 대부분 한 번 넣고 다시 찾지 않습니다. 필요하면 솔버 API로(Concert `IloRange` + `setName`, MathOpt `AddLinearConstraint(식, 이름)`) |
| 쌍대값·감소비용을 키로 읽기 | 드문 작업. 필요할 때 솔버 API로(Concert `getDuals`, MathOpt `dual_values()` 등) |
| 변수 덧붙이기(열 생성) | 구조 불변. 새 열은 새 집합 + 새 VarTable |
| 파이프 문법(`S \| where(p)`) | 같은 일에 두 번째 방법을 두지 않음 |
| 계수 배열(`IloScalProd`식) | 계수는 term에서 엔티티로 계산 |
| 변수 중복 생성 검사 | 문서 규칙 4로 대신함 |
| 제약 프로그래밍(구간 변수, `noOverlap`, 누적 함수) | 대수적 모형 계층의 범위 밖. 집합 계층은 솔버와 무관하므로 CP Optimizer나 CP-SAT로 스케줄링을 짤 때도 `makeSet`, `groupBy`, `ordered()`로 만든 행을 그 API에 그대로 넘겨 쓸 수 있습니다 |

## 공개 API 전체 목록

**열** (`column.hpp`)
| | |
|---|---|
| `Col<T, "name">` | 열 타입. `Col(T)`(암묵), `operator T()`, `get()`, 포인터면 `->`, `*`, `==`, 기본 생성 |
| `Col<...>::kName` | 열 이름 (`std::string_view`) |
| `ColumnName` | `Col`의 이름 템플릿 인자 타입 (문자열 리터럴) |
| `keyName(Col)` | 열의 이름 토큰. 값에 `keyName`이 있으면 그것, 없으면 값 자체 |
| `std::hash<Col<...>>` | 열을 `std::unordered_map` 키 등으로 쓸 수 있음 |
| `PredicateColumns<F>` | 템플릿 `operator()` 함수 객체가 읽는 열을 선언하는 특수화 지점 |

**집합 만들기** (`set.hpp`)
| | |
|---|---|
| `makeSet<Cols...>(sources...)` | 집합 빌더 |
| 빌더 `.where(p)` | 술어 하나 추가 (여러 개면 이어서 호출) |
| 빌더 `.collect()` | `IndexSet`으로 확정 |

**집합** (`indexed.hpp`, 구현은 `set.hpp`/`algebra.hpp`/`grouped.hpp`/`ordered.hpp`)
| | |
|---|---|
| `IndexSet<std::tuple<Cols...>>`, 별칭 `SetOf<Cols...>` | 읽기 전용 집합. 기본 생성(빈 집합), 복사 O(1) |
| `begin()`, `end()`, `size()` | random access 순회 |
| `where(p...)`, `orderBy(key)`, `distinct()`, `select<Cols...>()`, `select(expr)` | 파생 → `IndexSet` |
| `groupBy<Cols...>()` | → `Grouped` |
| `membership()` | → `Membership` |
| `ordered()` | → `Ordered` |
| `assertUnique<Cols...>()` | 디버그 검사, 같은 집합 반환 |
| `unite(A, B)`, `intersect(A, B)`, `difference(A, B)`, `concat(A, B)` | 이항 연산 → `IndexSet` |

**Membership** (`algebra.hpp`)
| | |
|---|---|
| `(열...)` / `(행)` | 소속 여부 `bool` |

**Grouped** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()` | 존재하는 묶음 전부 (첫 등장 순). 원소 `Group`은 `[열..., rows]`로 풀림 |
| `Key`, `Group{key, tuples}` | 묶음 키 튜플, 묶음 하나 |
| `slice(열...)`, `slice(key)` | 그 키의 `RowSlice` (없으면 빈 것). `key`는 `keyAt()`이나 `Group::key`에서 |
| `contains(열...)`, `sliceSize(열...)` | 키 존재, 묶음 크기 (`key`도 받음) |
| `size()`, `empty()` | 묶음 수 |
| `keyAt(g)`, `at(g)` | g번째 묶음의 키, 묶음 (범위 밖이면 `std::out_of_range`) |
| `regrouped()` | 묶음별로 모은 새 `IndexSet` |

**RowSlice** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()`, `size()`, `empty()`, `operator[](i)`, `front()`, `back()` | random access 뷰, 기본 생성(빈 것) |

**Ordered** (`ordered.hpp`)
| | |
|---|---|
| `ord(열...)` | 위치 (`std::optional<std::size_t>`, 없으면 비어 있음). 아래 모두 열 대신 행 하나도 받음 |
| `next(열...)`, `next(열..., steps)`, `prev(...)` 같은 꼴 | 이웃 행 포인터 또는 `nullptr` |
| `nextCyclic(...)`, `prevCyclic(...)` | 끝에서 처음으로 돌아가는 이웃 |
| `first()`, `last()`, `at(pos)` | 처음, 끝, 위치로 |
| `isFirst(열...)`, `isLast(열...)`, `contains(열...)` | 판정 |
| `size()`, `empty()`, `members()` | 크기, 중복 제거된 집합 |

**변수** (`concert.hpp`, 네임스페이스 `dreamor::concert`)
| | |
|---|---|
| `VarTable<Keys...>{env, set, lb = 0, ub = IloInfinity, type = ILOFLOAT, name = nullptr}` | lb, ub는 숫자 또는 열 함수 |
| `VarTable<Keys...>{semiContinuous, env, set, sclb, ub, type, name}` | 반연속 |
| `Key` | `std::tuple<Keys...>` |
| `(keys...)`, `(row)` | 변수: `vYield(m, i, t)`, `vYield(row)` (없으면 `std::out_of_range`, 모든 백엔드 공통) |
| `contains(keys...)`, `contains(row)`, `size()`, `keys()` | |
| `begin()`, `end()` | `(row, var)` 순회 |
| `solution(algorithm)` | → `ValueTable` |
| `SemiContinuous`, `semiContinuous` | 반연속 생성자 태그 |

**값** (`value_table.hpp`, 네임스페이스 `dreamor`, 백엔드 공통)
| | |
|---|---|
| `ValueTable<Keys...>` | `(keys...)`/`(row)`로 값: `yieldPlan(m, i, t)` (없으면 `std::out_of_range`), `contains`, `size()`, `keys()`, `(row, value)` 순회 |

**합** (`concert.hpp`, 네임스페이스 `dreamor::concert`)
| | |
|---|---|
| `sumOf(env, rows, term)`, `sumOf<Col>(env, container, term)` | 합 빌더. term은 열 함수 또는 `VarTable` 자체 |
| 빌더 `.where(p...)`, `.split(p...)`, `.build()` | 거르기, 나눠 합, `IloExpr` 또는 `std::array<IloExpr, N>` |

**MathOpt 백엔드** (`mathopt.hpp`, 네임스페이스 `dreamor::mathopt`)
| | |
|---|---|
| `VarTable<Keys...>{model, set, lb = 0, ub = +inf, is_integer = false, name = nullptr}` | `model`은 `math_opt::Model&`; lb, ub는 숫자 또는 열 함수 |
| `(keys...)`, `(row)`, `contains`, `size`, `keys`, `begin`/`end` | Concert와 같음 (변수는 `math_opt::Variable`) |
| `solution(result)` | `math_opt::SolveResult` → `ValueTable`; 가능해가 없으면 `std::logic_error` |
| `sumOf(rows, term)`, `sumOf<Col>(container, term)` | 합 빌더; `build()`는 `math_opt::LinearExpression` (`split`이면 그 배열) |

## 테스트와 개발 환경

테스트는 프레임워크 없이 `tests/run_tests.sh` 하나로 돕니다. 컴파일러는 `CXX`로 고릅니다(기본 `g++`, C++20).

**컴파일러:** GCC 13 이상, Clang 18 이상. CI는 이 범위의 양 끝인 GCC 13과 16, Clang 18과 23만 검증하고 그 사이는
검증하지 않습니다. MSVC와 Apple Clang은 검증하지 않습니다. 최신 컴파일러를 로컬에서 돌려 보려면 Clang은
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

**열이 많은 집합의 컴파일 비용.** N열 집합을 만들고, 거르고, 묶고, `VarTable`을 올리고, 합을 내는 TU 하나를
컴파일한 결과입니다(`-std=c++20 -O2 -c`, CPU 시간과 최대 메모리. AMD Ryzen 7 5700X, WSL2 Ubuntu 24.04, 2026-10-22).
`tests/test_arity.cpp`가 테스트를 돌릴 때마다 63열과 64열을 컴파일하므로 비용이 폭발하면 거기서 드러납니다.
`python3 tools/measure_compile_cost.py [compiler ...]`로 이 표를 다시 잴 수 있습니다.

| 열 수 | g++ 13.3 | clang++ 18.1 | clang++ 23.1 |
|---|---|---|---|
| 4 | 2.7 s, 285 MB | 2.7 s, 206 MB | 2.8 s, 229 MB |
| 16 | 3.2 s, 327 MB | 3.3 s, 233 MB | 3.2 s, 254 MB |
| 32 | 3.8 s, 363 MB | 4.4 s, 284 MB | 4.3 s, 306 MB |
| 63 | 6.0 s, 494 MB | 8.0 s, 451 MB | 7.7 s, 462 MB |
| 64 | 6.1 s, 500 MB | 8.2 s, 459 MB | 7.8 s, 470 MB |

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

## 벤치마크

`bench/`는 dreamor를 손으로 짠 코드와 비교합니다. 솔버 없이, 같은 행·같은 변수·같은 식을 만드는 세 구현을
실험계획법으로 잽니다.

- `dreamor`
- `map`: `examples/production_planning_plain.hpp`처럼 `std::map`과 루프로 짠 코드
- `flat`: 병렬 벡터, CSR, 압축 키 해시로 손 최적화한 코드

변수와 식은 세 구현이 똑같이 만들어서 솔버 몫은 비교에서 상쇄됩니다. 남는 것은 생산계획 예제의 모델 생성 작업입니다.
집합 구성, 변수 생성, 세 가지 모양의 묶기, 계수가 있거나 없는 묶음 합, 다른 집합을 따라가는 slice, 조회, 메모리가
여기에 해당합니다. dreamor와 `flat`은 일곱 요인의 분해능 V 반쪽 설계로 비교합니다.
- 행 수(1e5~1e7)
- K2 소유
- 밀도
- 팬아웃 분포
- id가 행이 나오는 순서와 맞는지
- 묶음 크기
- 한 집합 위의 변수 묶음 수

`map`은 예제와 같은 모양의 셀에서만 돌립니다. 실제 솔버는 넣지 않습니다. 솔버의 비용은 컨테이너가 아니라 솔버 내부와
머신에 따라 움직이기 때문입니다. 실제 백엔드는 실험이 끝난 뒤 `bench/solver/`로 따로 봅니다. 결과는 수식으로
계산되는 엑셀 보고서로 나옵니다.

```bash
bench/build.sh
cd bench && python3 run_doe.py --reps 2 --map   # results/<date>_<commit>_N1e5-1e6-1e7_half/; then make_report.py on its result.csv
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

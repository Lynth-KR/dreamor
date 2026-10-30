# 가이드

[English](../guide.md) · **한국어**

dreamor의 각 부분을 모형이 쓰는 순서대로 설명합니다. 열과 엔티티, 집합 만들기와 파생, 묶기, 변수족, 합, 제약식과
해 읽기입니다. [README의 빠른 시작](../../README.ko.md#빠른-시작)은 이것들을 한 모형에 모아 보여 주고,
[API 레퍼런스](reference.md)는 모든 멤버를 표로 정리하며, [철학](philosophy.md)은 원칙과 사용 규칙을 다룹니다.

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
  두면 그걸 쓰고, 없으면 엔티티의 `primaryKey()`를 씁니다(다음 절, 디버그 빌드는 열마다 한 번 알립니다).
  `std::format`으로 출력되는 값(정수, 문자열)은 훅이 필요 없습니다.
- **키로 찾는 곳은 열 값만 받습니다.** `vYield(m, i, t)`, `yieldPlan(m, i, t)`, `slice(i, t)`, `prev(t)`,
  `in_routes(m, i)`는 키의 열을 선언 순서대로, 또는 집합의 행 하나를 받습니다. `Col`이 값에서 암묵 변환되더라도
  맨 값(`x(2, 1)`, 맨 포인터)과 중괄호(`slice({i, t})`)는 받지 않고, 무엇을 바꾸라는지 한 문장으로 멈춥니다.
  값이 int인 열 둘이 자리를 바꿔도 조용히 통과하는 일을 막기 위해서입니다. 값에서 시작한다면 `PeriodCol{2}`처럼
  감쌉니다.

## 엔티티 — EntityVector

엔티티 포인터를 키로 쓰는 집합은 엔티티 하나가 객체 하나라는 데 기댑니다. 머신 하나에 객체가 둘이면, 그 위에
만든 모든 집합에게는 머신이 둘입니다. `dreamor::EntityVector<T>`는 엔티티가 자기 타입 옆에 선언한 기본 키로
각 엔티티를 한 번만 담습니다. 선언 방식은 `keyName`과 같습니다.

```cpp
inline const std::string& primaryKey(const Machine& m) { return m.name; }     // found by ADL

dreamor::EntityVector<Machine> plant;
const auto [first, inserted] = plant.emplace(Machine{"M1", 40.0, 5.0, {}});   // inserted == false: M1 was there
const auto sRUN = dreamor::makeSet<MachineCol, PeriodCol>(plant, periods).collect();
plant.modify(first, [](Machine& m) { m.setup = 4.0; });                        // the one way to change one
```

- 읽기는 `std::vector`처럼(인덱스, 삽입 순서 순회, `size()`), 넣기는 `std::set`처럼 합니다. `insert`와 `emplace`는
  `{그 키의 원소, 들어갔는지}`를, `find(key)`는 그 원소나 null을, `contains(key)`는 있는지를 돌려줍니다. 원소는
  주소로 줍니다. 반복자는 다음 삽입에서 무효가 되지만 주소는 유지되기 때문입니다.
- 원소는 움직이지 않고(내부는 `std::deque`) 지우지도 않으므로, 모델에 넘긴 포인터는 계속 그 엔티티를 가리킵니다.
  복사는 막혀 있습니다(복사본은 모든 엔티티의 두 번째 객체입니다). 이동해도 원소는 제자리에 있습니다.
- 접근은 const입니다. `modify(entity, fn)`이 원소 하나를 제자리에서 바꾸며, 기본 키는 그대로여야 합니다(디버그
  빌드가 확인). 집합은 스냅숏이라 이미 만든 집합이나 모델은 다시 계산되지 않습니다. 그래서 `modify`는 모델을 만든
  뒤 결과를 적어 넣을 때 쓰며, 결과는 보통 `ValueTable`에 두는 편이 맞습니다.
- 복합 키는 `std::tuple`입니다: `return std::tuple{r.plant, r.code};`.
- 필수가 아니라 선택입니다. 아래 계약을 지키는 컨테이너라면 무엇이든 소스가 됩니다.

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
- **어떤 소스도 같은 원소를 두 번 담지 않습니다**: 컨테이너, 의존 소스가 돌려주는 범위, 집합 모두. 엔티티는 객체
  하나이고 컨테이너 하나가 담으므로, 머신 하나에 객체가 둘이면 그것도 반복입니다(엔티티는 `EntityVector`가
  지켜 줍니다). 표준 라이브러리처럼 이것은 전제조건이고, 어기면 미정의 동작입니다. 디버그 빌드(`NDEBUG` 없음)는
  어긴 소스를 메시지로 알리고 멈춥니다. 그 대가로 집합은 같은 행을 두 번 담지 않고(아래 어떤 연산도 반복을
  만들지 않습니다), VarTable의 키 하나에는 변수가 정확히 하나입니다.
- 결과 행 순서는 생성 순서이며, 어떤 열이 가장 빨리 변하는지는 고정이 아닙니다. 순서가 의미를 가지면
  `orderBy`로 정렬하세요.
- 집합 타입을 적을 때는 `dreamor::SetOf<MachineCol, ItemCol>`을 씁니다.

## 방문 순서 유도하기

`makeSet`은 소스를 방문할 순서를 컴파일 시점에 소스의 열만 보고 정합니다. 술어는 읽는 열이 모두 정해지는 즉시
돌고, 술어를 더 일찍 돌게 하는 소스가 먼저 옵니다. 크기는 모르므로, 50행에 하나만 남기는 술어와 모두 남기는
술어를 구별하지 못합니다. 어느 쪽이 걸러내는지 안다면 그 부분을 먼저 집합으로 만들어 넘기세요.

```cpp
// {m in MACHINES, i in ITEMS, t in PERIODS: 30 * time[i] <= capacity[m] and demand[i,t] > 0}
const auto fits_batch = [](MachineCol m, ItemCol i) { return 30 * i->time <= m->capacity; };
const auto sMAKE = dreamor::makeSet<MachineCol, ItemCol, PeriodCol>(machines, items, periods)
                       .where(fits_batch)
                       .where(is_open)
                       .collect();

// The same rows in two steps: is_open over items x periods alone, then fits_batch over what passed
const auto sOPEN        = dreamor::makeSet<ItemCol, PeriodCol>(items, periods).where(is_open).collect();
const auto sMAKE_STAGED = dreamor::makeSet<MachineCol>(machines, sOPEN).where(fits_batch).collect();
```

- **같은 집합입니다.** 행도 타입도 같습니다. 집합의 열은 소스 순서대로 오므로 `machines`를 앞에 두면
  `(machine, item, period)`가 됩니다. 행 순서만 다를 수 있습니다(중요하면 `orderBy`).
- **단계가 정하는 것**은 어느 술어가 어느 곱 위에서 도느냐이지, 루프의 순서가 아닙니다. 각 `makeSet` 안에서는
  여전히 planner가 고릅니다. 위에서 `is_open`은 배치가 맞는 (기계, 품목, 기간)마다가 아니라 (품목, 기간)마다 한 번
  돕니다. 열린 쌍이 적고 대부분의 배치가 맞는다면 일의 대부분이 여기서 줄어듭니다.
- **단계는 메모리에 놓이는 집합입니다.** 작은 쪽일 때 이득입니다.
- **순서를 지정하는 옵션은 없습니다.** 모델에 적어 넣은 순서는 데이터를 따라가지 못하고, 더 나은 planner의 덕도
  보지 못합니다. 단계는 모델을 읽듯 읽히는 평범한 코드입니다.

## 집합에서 집합 파생하기

집합은 읽기 전용입니다(`begin`/`end`/`size`, range-for와 읽기 전용 알고리즘). 대신 **파생**합니다.
복사는 O(1)이고 행을 공유합니다.

| 멤버 | 하는 일 | AMPL |
|---|---|---|
| `S.where(p, q)` | 술어를 모두 통과한 행 | `{(m,i) in S: p and q}` |
| `S.select<A, B>()` | 열 투영(중복 제거). 모든 열을 다른 순서로 고르면 열 순서 바꾸기 | `setof {(m,i,t) in S} (m,i)` |
| `S.select(expr)` | 행마다 `expr`이 만든 행(중복 제거) | `setof {(i,j) in S} (j,i)` |
| `S.orderBy(key)` | `key(row)`로 안정 정렬 | |
| `S.groupBy<A>()` | 묶기 (다음 절) | 슬라이스 |
| `S.membership()` | O(1) 소속 판정 술어. 다른 집합의 `where`에 넣음 | `(m,i) in S` |
| `S.ordered()` | ord / next / prev / nextCyclic / prevCyclic | ordered set |
| `S.ordered<A>()` | `A`가 같은 행끼리의 그룹마다 같은 일 | `next(t, S[a])` |

| 자유 함수 | |
|---|---|
| `dreamor::unite(A, B)` | A ∪ B |
| `dreamor::intersect(A, B)` | A ∩ B |
| `dreamor::difference(A, B)` | A \ B |
| `A == B` | 같은 행, 순서 무관 (순서까지는 `std::ranges::equal(A, B)`) |

```cpp
const auto sPEAK_PRODUCE  = sPRODUCE.where(is_peak);                  // 수요가 몰리는 기간의 생산 행
const auto sMACHINE_ITEM  = sPRODUCE.select<MachineCol, ItemCol>();  // (기계, 품목)만
const auto in_routes      = sROUTES.membership();
const auto orderedPeriods = sPERIODS.ordered();
if (const auto* prevPeriod = orderedPeriods.prev(t)) { /* 직전 기간 */ }
```

`ordered()`는 중복을 없앤 집합 위의 위치 뷰(`Ordered`)입니다. 위치는 0부터 셉니다. `next`/`prev`/
`nextCyclic`/`prevCyclic`는 원소 뒤에 걸음 수를 받을 수 있고(`next(t, 2)`, 음수 가능) 행의 포인터를 돌려주며,
경계를 넘거나 집합에 없는 원소면 `nullptr`입니다. `ordered<A>()`는 같은 일을 `A`가 같은 행끼리의 그룹 안에서 합니다
(AMPL의 `next(t, S[a])`). 그래서 이동은 그룹 끝에서 멈추고(cyclic이면 그룹 안에서 돌고), `ord`는 그룹 안의 위치,
`isFirst`/`isLast`는 그룹의 양 끝입니다. 집합 전체의 위치를 뜻하는 `first()`, `last()`, `at()`은 없습니다.
나머지는 [공개 API 전체 목록](reference.md#공개-api-전체-목록)에 있습니다.

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
- 이름은 `이름_키0_키1...` (`Yield_M0_I3_2`). 키 토큰은 `keyName()` 훅(없으면 `primaryKey()`)에서 옵니다. `nullptr`이면 이름 없음.
  열을 `std::format`에 넘기면 같은 토큰이 출력되므로, 행 이름도 변수 이름과 같은 방식으로 지을 수 있습니다:
  `dreamor::exprName("Cap", m, t)` (같은 토큰을 스레드의 버퍼에서, 서식 문자열 없이). 다른 모양의 이름은
  `std::format`(또는 `FormatBuffer`의 `format`)으로: 열은 같은 토큰으로 출력됩니다.
- VarTable은 생성자에서 집합을 받으므로 집합이 먼저 만들어져 있어야 합니다. 모델을 구조체로 묶을 때는 집합을
  VarTable보다 먼저 선언한 멤버로 두거나(멤버는 선언 순서대로 만들어집니다), 기반 클래스에 둡니다(기반 클래스가
  멤버보다 먼저 만들어집니다). 예제의 `Sets` / `Model`은 기반 클래스를 씁니다.

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
for (const auto& row : vYield.keys().where(in_maintenance)) {
  vYield(row).setUB(0);
}
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
라이브러리에 제약 테이블은 없습니다(이유는 [하지 않는 것](philosophy.md#하지-않는-것)).

```cpp
// 집합 위의 제약족: 묶음마다 하나
for (const auto& [machine, period, rows] : producedByMachinePeriod) {
  model.add(dreamor::concert::sumOf(env, rows, machine_usage).build() <= machine->capacity);
}

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

## 이름

모형은 변수와 행을 한 규칙으로 짓습니다: `<family>_<key0>_<key1>...`, 키마다 그 토큰(`keyName()`, 없으면
`primaryKey()`, 없으면 값). 도구 둘이면 충분하고, 나머지 둘은 예외를 위한 것입니다.

| 도구 | 누가 쓰나 | 하는 일 |
|---|---|---|
| `VarTable` | 모든 모형 | 변수 이름을 스스로 짓습니다(`Yield_M1_I3_2`). 부를 것이 없습니다 |
| `dreamor::exprName(family, keys...)` | 그 밖에 이름이 필요한 모든 것 | 같은 규칙으로 이름 하나: 행, 또는 집합 밖에서 만든 변수. `model.add(load <= cap).setName(dreamor::exprName("Capacity", m, t))`. 키가 없으면 문자열 리터럴(`"SetupBudget"`) |
| `general::FormatBuffer<N>::format` | 드물게 | 모양이 다른 이름(`x[1,2]`, 숫자를 `{:.2f}`로)을 서식 문자열로. 열은 토큰으로 출력 |
| `general::NameBuilder<N>` | 더 드물게 | 조각을 이어 붙여 만드는 이름 - 예컨대 트리를 한 단계씩 내려가며. `VarTable`과 `exprName`이 이것으로 짓습니다 |

`exprName`과 `format`은 스레드의 버퍼에 만들고, 같은 스레드의 다음 호출까지 유효한 포인터를 돌려줍니다. 복사해 가는
`setName`, `AddLinearConstraint`, `addConstr`에 바로 넘기세요. `exprName`은 서식 문자열을 읽지 않아서
(`std::format`은 호출마다 읽습니다) 행 수만 개에 이름을 붙이는 모형에서 시간이 몇 분의 일로 줍니다.

## 해 읽기

```cpp
const auto yieldPlan = vYield.solution(cplex);   // getValues 한 번으로 테이블 전체
yieldPlan(m, i, t);                          // 키로 값
for (const auto& [row, qty] : yieldPlan) { ... } // 집합 순서대로
```

- 결과(`ValueTable`)는 double만 들고 있어서 `cplex`나 `env`가 끝난 뒤에도 쓸 수 있습니다. 백엔드와 무관한
  같은 타입입니다.
- `ValueTable`은 집합 위의 데이터, AMPL의 `param {S}`이기도 합니다. `dreamor::ValueTable<MachineCol, PeriodCol>
  hours{sRUN, available_hours}`는 `sRUN`의 각 행에 값을 줍니다. 값은 `VarTable`의 bound처럼 숫자이거나 행의 열을
  받는 콜러블이고, 읽는 법은 같습니다(키나 행으로, 집합 밖이면 `std::out_of_range`). 그 자체가 열 콜러블이라서
  콜러블이 들어갈 자리에 그대로 씁니다: `VarTable`의 bound(`VarTable{env, sRUN, 0, hours}`), 다른 `ValueTable`의 값.
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

## 열이 많은 집합의 컴파일 비용

N열 집합을 만들고, 거르고, 묶고, `VarTable`을 올리고, 합을 내는 TU 하나를
컴파일한 결과입니다(`-std=c++20 -O2 -c`, `NDEBUG` 없이, 즉 `-DNDEBUG`이면 빠지는 디버그 빌드의 계약 확인을 포함해서. CPU
시간과 최대 메모리. AMD Ryzen 7 5700X, WSL2 Ubuntu 24.04, 2026-10-25).
`tests/test_arity.cpp`가 테스트를 돌릴 때마다 63열과 64열을 컴파일하므로 비용이 폭발하면 거기서 드러납니다.
`python3 tools/measure_compile_cost.py [compiler ...]`로 이 표를 다시 잴 수 있습니다.

| 열 수 | g++ 13.3 | clang++ 18.1 | clang++ 23.1 |
|---|---|---|---|
| 4 | 3.3 s, 315 MB | 3.0 s, 216 MB | 3.1 s, 230 MB |
| 16 | 3.8 s, 331 MB | 3.6 s, 246 MB | 3.7 s, 258 MB |
| 32 | 4.6 s, 390 MB | 4.9 s, 301 MB | 4.7 s, 312 MB |
| 63 | 7.6 s, 569 MB | 8.8 s, 469 MB | 8.3 s, 474 MB |
| 64 | 7.8 s, 580 MB | 9.0 s, 477 MB | 8.5 s, 482 MB |

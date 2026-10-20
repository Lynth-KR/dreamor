# OR-Tools MathOpt 예제를 dreamor로 옮기기

[English](README.md)

OR-Tools MathOpt에 들어 있는 C++ 예제 세 개를 dreamor 방식으로 다시 썼습니다. 목적은 속도가 아닙니다
(인스턴스가 아주 작습니다). 인덱스 집합을 루프와 해시맵 속에 숨겨 두지 않고 선언하면 모형 코드가 어떻게
달라지는지를 보여주는 것입니다.

원본은 google/or-tools@100f66e의 [`ortools/math_opt/samples/cpp`](https://github.com/google/or-tools/tree/100f66e6242ab8bf8d32feb8f3bf086db66ae2b5/ortools/math_opt/samples/cpp)에
있습니다. 각 포팅본은 원본의 라이선스 머리말을 유지하고 무엇을 바꿨는지 밝힙니다.

모형 밖의 것은 원래대로 두었습니다: 플래그, 데이터, 풀이 루프, LaTeX 내보내기. 그 밖에 바뀐 것은 출력 하나로,
`std::cout` 대신 `dreamor/logger/logger_light.hpp`를 씁니다. LaTeX 출력은 파이프로 넘기는 용도라 그대로
표준 출력입니다.

| 예제 | 포팅이 보여주는 것 |
|---|---|
| `cocktail_hour.cc` | 2차원 집합 위의 제약족, 열 함수로 준 범위, 집합 순서로 해 읽기 |
| `time_indexed_scheduling.cc` | 0으로 고정한 변수 대신 희소 인덱스 집합, 기존 집합에서 만든 집합, 드라이버 집합과 묶기의 차이 |
| `lagrangian_relaxation.cc` | 두 번의 묶기로 쓴 흐름 보존, 집합과 테이블을 담은 구조체로서의 모형 |

세 포팅본의 결과는 원본과 같습니다. 같은 커밋의 원본을 같은 OR-Tools 릴리스(v9.15)로 빌드해 대조했습니다.
- **`cocktail_hour`**: text, analysis, LaTeX 모드에서 같은 메뉴.
- **`lagrangian_relaxation`**: 쌍대화할 제약의 모든 조합에서 같은 MIP, LP, 라그랑주 한계.
- **`time_indexed_scheduling`**: 테스트 인스턴스와 시드를 고정한 무작위 인스턴스에서 같은 일정 목적값.

`tests/run_tests.sh --mathopt`가 셋 모두 빌드하고 실행합니다.

## cocktail_hour

칵테일을 최대한 많이 만들 수 있도록 살 재료를 고르는 문제입니다.

```
set RECIPE := {c in COCKTAILS, i in INGREDIENTS_OF[c]};
subject to NeedAll {(c,i) in RECIPE}: Make[c] <= Buy[i];
```

원본은 변수족마다 `flat_hash_map<std::string, Variable>`을 하나씩 둡니다. 레시피 제약은 칵테일 변수를 만드는
루프 안에 들어 있고, 재료를 이름으로 찾아 걸립니다.

```cpp
for (const Cocktail& cocktail : all_cocktails) {
  const double lb = required_cocktails.contains(cocktail.name) ? 1.0 : 0.0;
  const double ub = blocked_cocktails.contains(cocktail.name) ? 0.0 : 1.0;
  const math_opt::Variable v = model.AddIntegerVariable(lb, ub, cocktail.name);
  for (const std::string& ingredient : cocktail.ingredients) {
    model.AddLinearConstraint(v <= gtl::FindOrDie(ingredient_vars, ingredient));
  }
  gtl::InsertOrDie(&cocktail_vars, cocktail.name, v);
}
```

포팅본은 집합을 먼저 선언하고, 모형의 각 부분을 한 번씩만 씁니다.

```cpp
const auto sRECIPE =
    dreamor::makeSet<CocktailCol, IngredientCol>(all_cocktails, ingredients_of).collect();
const dreamor::mathopt::VarTable<CocktailCol> vMake{
    model, sCOCKTAILS, required, allowed, /*is_integer=*/true, "Make"};

for (const auto& [c, i] : sRECIPE) {
  model.AddLinearConstraint(vMake(c) <= vBuy(i));
}
```

달라지는 점은 이렇습니다.
- **범위.** 조건부 범위가 생성 루프 안의 지역 변수가 아니라, 테이블에 넘기는 열 함수(`required`, `allowed`)가
  됩니다.
- **변수 생성과 제약.** 둘이 각각의 문장이 됩니다. `InsertOrDie`/`FindOrDie`도 없습니다. 레시피에
  `INGREDIENTS`에 없는 재료가 있으면, 원본이 `FindOrDie`에서 멈추던 자리에서 `vBuy(i)`가 `std::out_of_range`를
  던집니다.
- **해 읽기.** 원본은 해시맵에 순서가 없어서, 메뉴를 만들 때 `kIngredients`와 `AllCocktails()`를 다시 돌며
  변수마다 두 번 찾습니다(`variable_values().at(map.at(name))`). 포팅본은 집합 순서의 `ValueTable`인
  `vBuy.solution(result)`를 돌면 되고, 그 순서가 곧 선언 순서입니다.
- **합.** 예산 합과 목적식 합이 해시맵 순서가 아니라 집합 순서로 돕니다.

재료 열은 이름 자체를 담는 값 열(`Col<std::string_view, "ingredient">`)이라, 데이터는 원본과 똑같이 둡니다.
`ingredients_of`는 칵테일이 들고 있는 문자열을 가리키는 `std::span`을 돌려줍니다. 벡터를 값으로 돌려주면
`string_view`가 매달리게 됩니다.

## time_indexed_scheduling

release time이 있는 작업들을 기계 하나에 배치해 완료 시각 합을 최소화합니다. 결정 변수는 이진 `x[j,s]`
("작업 j가 시각 s에 시작")입니다.

원본은 모든 작업과 시간축 위의 모든 시각에 `x`를 만들고, release time 이전 것은 0으로 고정합니다. 중첩 금지
제약은 시각마다 창을 도는 삼중 루프로 씁니다.

```cpp
for (int t = 0; t < kTimeHorizon; ++t) {
  math_opt::Variable v = model.AddBinaryVariable();
  ...
  if (t < jobs[i].release_time) {
    model.set_upper_bound(v, 0.0);
  }
}
...
for (int t = 0; t < kTimeHorizon; ++t) {
  math_opt::LinearExpression conflicts;
  for (int i = 0; i < jobs.size(); ++i) {
    for (int s = std::max(0, t - jobs[i].processing_time + 1); s <= t; s++) {
      conflicts += x[i][s];
    }
  }
  model.AddLinearConstraint(conflicts <= 1.0);
}
```

포팅본은 release 조건을 인덱스 집합에 넣고, "실행 중" 관계를 그 집합 위의 집합으로 만듭니다.

```cpp
// set START := {j in JOBS, s in release[j]..T-1};
// set RUNNING := {(j,s) in START, t in s..min(s+p[j], T)-1};
const auto sSTART = dreamor::makeSet<JobCol, StartCol>(job_ids, starts_of).collect();
const auto sRUNNING = dreamor::makeSet<TimeCol>(sSTART, running_at).collect();
...
for (const auto& [t, rows] : sRUNNING.groupBy<TimeCol>()) {
  model.AddLinearConstraint(dreamor::mathopt::sumOf(rows, vStart).build() <= 1.0);
}
```

달라지는 점은 이렇습니다.
- **변수 수.** release 이전 변수는 아예 만들어지지 않습니다. 기본 크기인 작업 30개 무작위 인스턴스에서 모형은
  변수 18,060개 대신 12,896개, 제약 632개 대신 615개입니다.
- **중첩 금지 제약.** "시각 t에 실행 중인 시작들"은 집합 `RUNNING`을 `t`로 묶은 것입니다. 아무 작업도 돌 수
  없는 시각에는 제약이 없습니다. 그 시각의 합은 비어 있어서 제약이 아무 말도 하지 않기 때문입니다.
- **"작업은 한 번 시작"의 순회 대상.** 이 제약은 `START`의 묶음이 아니라 `JOBS`를 돌며 `slice()`합니다. 가능한
  시작이 하나도 남지 않은 작업은 묶음에서 조용히 빠져 버리기 때문인데, 이렇게 쓰면 그 작업에 `0 = 1`이 걸려
  원본처럼 불능이 됩니다.
- **목적식.** `sumOf(sSTART, completion_time)`이고, `completion_time`이 행의 열에서 `(s + p[j]) * x[j,s]`를
  읽습니다.

dreamor와 상관없는 수정이 하나 있습니다. 원본은 목적값을 `int sum_of_completion_times = expression.Evaluate(...)`로
저장해서 소수점 이하가 잘립니다. MIP의 이진 변수는 0과 1에 가까울 뿐이라, 합이 2893.9999999면 2893으로
보고됩니다. 포팅본은 반올림합니다.

## lagrangian_relaxation

자원 제약 두 개가 있는 최단경로를 MIP로, LP 완화로, 라그랑주 완화로 풉니다. 원본은 같은 모형을
`SolveMip`과 `CreateShortestPathModel` 두 곳에서 같은 코드로 두 번 만듭니다.

원본은 흐름 보존을 노드 번호로 인덱싱한 식 벡터 두 개에 쌓습니다.

```cpp
std::vector<math_opt::LinearExpression> out_flow(graph.num_nodes);
std::vector<math_opt::LinearExpression> in_flow(graph.num_nodes);
for (int arc_index = 0; arc_index < graph.arcs.size(); ++arc_index) {
  out_flow[graph.arcs[arc_index].i] += flow_model.flow_vars[arc_index];
  in_flow[graph.arcs[arc_index].j] += flow_model.flow_vars[arc_index];
}
```

포팅본은 호를 꼬리와 머리로 각각 묶고, 노드마다 보존식을 씁니다.

```cpp
const auto outOf = sARC_ENDS.groupBy<TailCol>();
const auto into = sARC_ENDS.groupBy<HeadCol>();
for (const auto& [n] : sNODES) {
  model->AddLinearConstraint(
      dreamor::mathopt::sumOf(outOf.slice(TailCol{n.get()}), vFlow).build() -
          dreamor::mathopt::sumOf(into.slice(HeadCol{n.get()}), vFlow).build() == rhs);
}
```

달라지는 점은 이렇습니다.
- **모형이 구조체가 됩니다.** 집합은 기반 클래스(`Network`)에, 테이블과 식은 파생 클래스(`FlowModel`)에 둡니다.
  그래서 MIP와 라그랑주 부분문제가 생성자 하나를 같이 쓰고, 다른 건 `integer` 하나뿐입니다.
- **호가 엔티티가 됩니다.** `flow_vars[arc_index]` 대신 `vFlow(a)`입니다. 변수를 도는 코드(완화, 다시 정수화)는
  테이블을 돕니다: `for (const auto& [arc, var] : vFlow)`.
- **꼬리와 머리.** `ARC_ENDS`의 열입니다. dreamor에는 파생 열이 없어서 각각 원소 하나짜리 종속 소스
  (`std::views::single(a->i)`)로 씁니다.
- **수명.** 집합이 `graph.arcs`를 가리키므로, 원본이 값으로 받던 그래프를 참조(`const Graph&`)로 받습니다. 복사본
  위에 만든 `FlowModel`은 그 복사본보다 오래 삽니다.
- **노드별 보존식.** `NODES`를 돌며 `slice()`하므로, 들어오는 호가 없는 출발점과 나가는 호가 없는 도착점에도
  행이 걸립니다.

이 예제는 포팅의 이득이 가장 작은 곳입니다. 원본의 노드별 식 벡터가 이미 O(호 수)로 짧아서, 포팅본은 같은 말을
하려고 열과 집합을 선언하는 데 몇 줄을 더 씁니다.

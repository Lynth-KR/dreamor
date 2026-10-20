# OR-Tools MathOpt samples, ported to dreamor

[한국어](README.ko.md)

Three of the C++ samples that ship with OR-Tools MathOpt, rewritten the way dreamor is meant to
be used. The point is not speed — the instances are tiny — but how the model code changes when
the index sets are declared instead of implied by loops and hash maps.

The originals are in [`ortools/math_opt/samples/cpp`](https://github.com/google/or-tools/tree/100f66e6242ab8bf8d32feb8f3bf086db66ae2b5/ortools/math_opt/samples/cpp)
at google/or-tools@100f66e. Each port keeps the original's license header and says what changed.
Everything around the model is left as it was: flags, data, the solve loop, LaTeX export. The
only other change is output, which goes through `dreamor/logger/logger_light.hpp` instead of
`std::cout` (LaTeX output still goes to standard out, since it is meant for a pipe).

| Sample | What the port shows |
|---|---|
| `cocktail_hour.cc` | A constraint family over a 2-index set; bounds as column functions; reading solutions in set order |
| `time_indexed_scheduling.cc` | A sparse index set instead of fixed-to-zero variables; a set built from an existing set; a driver set vs. grouping |
| `lagrangian_relaxation.cc` | Flow balance as two groupings; the model as a struct of sets and tables |

The three ports produce the same results as the originals, checked against the originals built
from the same commit on the same OR-Tools release (v9.15):
- **`cocktail_hour`**: the same menu in text, analysis and LaTeX modes.
- **`lagrangian_relaxation`**: the same MIP, LP and Lagrangian bounds for every combination of
  dualized constraints.
- **`time_indexed_scheduling`**: the same schedule objective on the test instance and on seeded
  random instances.

`tests/run_tests.sh --mathopt` builds and runs all three.

## cocktail_hour

Pick ingredients to buy so that as many cocktails as possible can be made.

```
set RECIPE := {c in COCKTAILS, i in INGREDIENTS_OF[c]};
subject to NeedAll {(c,i) in RECIPE}: Make[c] <= Buy[i];
```

The original keeps a `flat_hash_map<std::string, Variable>` per variable family. The recipe
constraint lives inside the loop that creates the cocktail variables, looking each ingredient up
by name:

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

The port declares the sets, then states each part of the model once:

```cpp
const auto sRECIPE =
    dreamor::makeSet<CocktailCol, IngredientCol>(all_cocktails, ingredients_of).collect();
const dreamor::mathopt::VarTable<CocktailCol> vMake{
    model, sCOCKTAILS, required, allowed, /*is_integer=*/true, "Make"};

for (const auto& [c, i] : sRECIPE) {
  model.AddLinearConstraint(vMake(c) <= vBuy(i));
}
```

What changes:
- **Bounds.** The conditional bounds become column functions (`required`, `allowed`) passed to
  the table, instead of locals computed inside the creation loop.
- **Variable creation and constraints.** These are separate statements. No `InsertOrDie` or
  `FindOrDie`: a recipe naming an ingredient that is not in `INGREDIENTS` throws
  `std::out_of_range` at `vBuy(i)`, where the original would `FindOrDie`.
- **Reading the solution.** The hash maps have no order, so the original re-walks `kIngredients`
  and `AllCocktails()` and looks every variable up twice
  (`variable_values().at(map.at(name))`). The port walks `vBuy.solution(result)`, a
  `ValueTable` in set order, which already is the declaration order.
- **Sums.** The budget and objective sums run in set order instead of hash-map order.

The ingredient column holds the name itself (`Col<std::string_view, "ingredient">`), so the
data stays exactly as in the original. `ingredients_of` returns a `std::span` over the
cocktail's own strings: returning the vector by value would leave the `string_view`s dangling.

## time_indexed_scheduling

Schedule jobs with release times on one machine to minimize the sum of completion times, with a
binary `x[j,s]` = "job j starts at time s".

The original creates `x` for every job and every time in the horizon, then fixes the ones
before the release time to zero. It states the no-overlap constraint for every time with a
triple loop over a window:

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

The port makes the release dates part of the index set and builds the "running at" relation
as a set over that set:

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

What changes:
- **Fewer variables.** Variables before the release time are never created. On the 30-job
  random instance (the default size), the model has 12,896 variables instead of 18,060, and 615
  constraints instead of 632.
- **No-overlap constraints.** "Which starts are running at time t" is the set `RUNNING`, grouped
  by `t`. Times when nothing can run get no constraint; there the sum would be empty and the
  constraint would say nothing.
- **"Each job starts once."** This constraint iterates `JOBS` and calls `slice()`, rather than
  iterating `START`'s groups. A job left with no feasible start would otherwise drop out of the
  model silently; this way it gets `0 = 1`, and the model is infeasible as in the original.
- **The objective.** It is `sumOf(sSTART, completion_time)`, where `completion_time` reads
  `(s + p[j]) * x[j,s]` off the row's columns.

The port has one fix that is unrelated to dreamor. The original stores the objective with
`int sum_of_completion_times = expression.Evaluate(...)`, which truncates. Because a MIP's
binaries are only nearly 0 and 1, a total of 2893.9999999 is reported as 2893. The port rounds
the value instead.

## lagrangian_relaxation

A shortest path with two resource constraints, solved as a MIP, as its LP relaxation and by
Lagrangian relaxation. The model is built twice in the original, by `SolveMip` and by
`CreateShortestPathModel`, from the same code.

The original accumulates flow balance into two vectors of expressions indexed by node:

```cpp
std::vector<math_opt::LinearExpression> out_flow(graph.num_nodes);
std::vector<math_opt::LinearExpression> in_flow(graph.num_nodes);
for (int arc_index = 0; arc_index < graph.arcs.size(); ++arc_index) {
  out_flow[graph.arcs[arc_index].i] += flow_model.flow_vars[arc_index];
  in_flow[graph.arcs[arc_index].j] += flow_model.flow_vars[arc_index];
}
```

The port groups the arcs by tail and by head, and states the balance per node:

```cpp
const auto outOf = sARC_ENDS.groupBy<TailCol>();
const auto into = sARC_ENDS.groupBy<HeadCol>();
for (const auto& [n] : sNODES) {
  model->AddLinearConstraint(
      dreamor::mathopt::sumOf(outOf.slice(TailCol{n.get()}), vFlow).build() -
          dreamor::mathopt::sumOf(into.slice(HeadCol{n.get()}), vFlow).build() == rhs);
}
```

What changes:
- **The model is a struct.** The sets live in a base class (`Network`) and the table and
  expressions in a derived one (`FlowModel`), so one constructor serves both the MIP and the
  Lagrangian subproblem. The constructor only differs in `integer`.
- **Arcs are entities.** `vFlow(a)` replaces `flow_vars[arc_index]`. Code that walks the
  variables (relaxing, re-integralizing) iterates the table: `for (const auto& [arc, var] : vFlow)`.
- **Tail and head are columns of `ARC_ENDS`.** dreamor has no derived columns, so each is a
  one-element dependent source (`std::views::single(a->i)`).
- **Lifetimes.** The sets point into `graph.arcs`, so the graph is passed by reference
  (`const Graph&`) where the original took it by value. A `FlowModel` built on a copy would
  outlive the copy.
- **Balance per node.** The constraint iterates `NODES` and slices, so the source (no arc in)
  and the sink (no arc out) still get their rows.

This sample is where the port gains least. The original's per-node expression vectors are
already O(arcs) and short, and the port spends a few lines declaring columns and sets to say the
same thing.

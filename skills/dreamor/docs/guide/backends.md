<!-- Copied from docs/guide/backends.md (blob c9abad782da3), tests/docs/highs/backends.cpp (blob 0f608bccacc2) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Backends

A backend writes the model's variable families and sums in a solver's own objects. dreamor has three backends: HiGHS,
Concert for CPLEX, and MathOpt for OR-Tools. The guide solves on HiGHS. HiGHS's own C++ API has no expressions in
which to write a row, so dreamor's HiGHS backend, `dreamor::highs`, adds them. A `HighsEnv` holds the model as you
write it, together with HiGHS's own `Highs` object, which solves the model. This page shows how to give HiGHS its
options, when the model reaches the `Highs`, how to use HiGHS's own API on the model, and how to catch a HiGHS call
that fails. The last section says what differs on Concert and MathOpt.

The examples use the [guide's model](index.md) on HiGHS:

- `p` is the plant: its items, its machines and its bill of materials. [Entities](entities.md) shows its items and
  machines.
- `quantity` is the bill of materials, a `ParamTable<double, cParent, cChild>`. [Data on a set](data-on-a-set.md)
  makes it.
- `byChain(p, quantity)` returns the model's sets as a `Sets`. [Set-builder notation](set-builder-notation.md) shows
  the chains that build them.
- `guide::highs::Plan` is the guide's plan on HiGHS. Its `env` is its `HighsEnv`, and `vMake`, `vSetup` and `vStock`
  are its variable families. The constructor gives its options to `env` first, then writes the families, the rows
  and the cost into `env`. [Variable families](variable-families.md) shows the plan, and
  [Constraints and objective](constraints-and-objective.md) its rows and its cost.
- `quiet` holds HiGHS's options, with HiGHS's log turned off.
- The code has `using namespace dreamor::highs;`, so it names `HighsEnv`, `sumExpr` and `addConstr` without their
  namespace. `HighsOptions`, `Highs` and `HighsModelStatus` are HiGHS's own types.

## Give HiGHS its options before the model

Suppose you want HiGHS to write no log, and to stop a solve after 60 seconds. To have the options hold from the
moment the model reaches HiGHS, give them to the environment when you construct it:

```cpp
HighsOptions options;
options.output_flag = false;  // no log
options.time_limit  = 60;     // seconds
guide::highs::Plan plan{byChain(p, quantity), quantity, options};
```

- `options.output_flag = false` turns HiGHS's log off, and `options.time_limit = 60` stops a solve after 60 seconds.
- `guide::highs::Plan plan{byChain(p, quantity), quantity, options}` constructs the plan on the model's sets. The
  plan constructs its environment first, as `env{options}`, and the `HighsEnv` constructor passes `options` to its
  `Highs` before any of the model is written.

The `Highs` of `plan.env` has the options before it has any of the model: no log, and a limit of 60 seconds. So the
options hold when HiGHS receives the model, as well as for every solve. The next section shows when the model reaches
the `Highs`.

For when the constructor passes the options, see the [`HighsEnv` constructor](../reference/dreamor-highs.md#highsenv-constructor-options),
and for why a `HighsEnv` cannot be copied or moved, see [`HighsEnv`](../reference/dreamor-highs.md#highsenv).

## Know when the model reaches the `Highs`

The plan's tables, its rows and its cost are written into the environment, not into the `Highs`. The `Highs`
receives the model later, all at once. Suppose that, after you build the plan, you add a row that keeps the stock of
every item over every period at 10 units or less. To know where that row goes, you need to know when the model
reaches the `Highs`:

```cpp
const Sets s = byChain(p, quantity);
guide::highs::Plan plan{s, quantity, quiet};  // written into plan.env: the Highs holds nothing yet
Highs& highs = plan.env.highs();              // the first reach passes the model whole

const auto kept = sumExpr(s.stock, plan.vStock).build();
addConstr(plan.env, kept <= 10, "StockCap");  // stock over every item and period, at most 10: a row at once
```

- `guide::highs::Plan plan{s, quantity, quiet}` writes the families, the rows and the cost into `plan.env`. The
  `Highs` holds nothing yet.
- `plan.env.highs()` returns the `Highs`. The first call hands the whole model to the `Highs` in one call to HiGHS.
- `sumExpr(s.stock, plan.vStock).build()` adds up the stock variables over the rows of `s.stock`.
  [Sums](sums.md) shows `sumExpr`.
- `addConstr(plan.env, kept <= 10, "StockCap")` adds the row `kept <= 10`, named `StockCap`. The model has already
  reached the `Highs`, so the row goes to the `Highs` at once.

After the first `highs()`, the `Highs` holds the whole plan. Its columns are the variables, one for each row of a
family's set: 15 for `vMake` and 15 for `vSetup` over `s.produce`, and 12 for `vStock` over `s.stock`, 42 columns.
Its rows are a balance row for each item and period, an hours row for each machine and period, and a setup row for
each row of `s.produce`: 12, 6 and 15, 33 rows. `StockCap` comes after them, as the 34th row. An option
that you set through `highs()` therefore reaches the `Highs` after the model. To have it hold before, pass it to the
constructor, as the section above does.

For every call that reaches the `Highs` for the first time, and what a write costs after it, see
[`HighsEnv`](../reference/dreamor-highs.md#highsenv) and [`highs`](../reference/dreamor-highs.md#highsenv-highs).

## Use HiGHS's own API for the rest

dreamor writes the model. Solving it, reading the result and changing the model afterwards are HiGHS's own calls on
the `Highs`. Suppose you want to know what the plan costs if M2 makes no plates in period 3. Fix M2's setup for the
plate in that period at 0, and solve again:

```cpp
Highs& highs = plan.env.highs();
highs.run();
const double cost = highs.getInfo().objective_function_value;  // 75.4

const HighsVar plateOnM2 = plan.vSetup(p.machines.at("M2"), p.items.at("plate"), 3);
highs.changeColBounds(plateOnM2.index(), 0, 0);  // no plates on M2 in period 3
highs.run();
if (highs.getModelStatus() != HighsModelStatus::kOptimal) {
  DREAMOR_LOG_WARNING("no plan without plates on M2 in period 3");
}
const double costWithout = highs.getInfo().objective_function_value;  // 79.8
```

- `highs.run()` solves the model, and `getInfo().objective_function_value` returns the cost of the plan found.
- `plan.vSetup` returns the `HighsVar` of the row of M2, the plate and period 3: whether M2 is set up for plates in
  period 3. Its `index()` returns the variable's column in the `Highs`.
- `changeColBounds` sets both bounds of that column to 0. M2 makes no plates without a setup, so it makes no plates
  in period 3.
- `highs.run()` solves the model again. If HiGHS finds no optimal plan, the code logs a warning.

The plan costs 75.4. Without plates on M2 in period 3, HiGHS finds an optimal plan that costs 79.8, so the code logs
no warning.

For what a `HighsVar` holds, see [`HighsVar`](../reference/dreamor-highs.md#highsvar) and
[`index`](../reference/dreamor-highs.md#highsvar-index). HiGHS's own calls are documented by HiGHS.

## Catch a HiGHS call that fails

A call to HiGHS can fail, for example if HiGHS rejects an option. If a call that dreamor makes to HiGHS returns an
error, dreamor throws a `HighsCallError`. Suppose your options hold a time limit of -1 by mistake. To report the
error, catch the exception where you build the model:

```cpp
HighsOptions options;
options.time_limit = -1;  // HiGHS takes no negative time limit
try {
  guide::highs::Plan plan{byChain(p, quantity), quantity, options};
  plan.env.highs().run();
} catch (const HighsCallError& error) {
  DREAMOR_LOG_ERROR("{}", error.what());  // dreamor::highs: Highs::passOptions returned Error
}
```

- Constructing the plan constructs its `HighsEnv`, which passes `options` to the `Highs` with HiGHS's
  `passOptions`.
- HiGHS rejects the negative time limit, so `passOptions` returns an error, and the `HighsEnv` constructor throws
  the exception.
- The `catch` logs the exception's message as an error line.

The log holds an error line, `dreamor::highs: Highs::passOptions returned Error`. The message names the HiGHS call
that failed and the status that the call returned.

For what a `HighsCallError` holds, see [`HighsCallError`](../reference/dreamor-highs.md#highscallerror). For every
call that throws one, such as the first `highs()` if HiGHS rejects the model, see
[Exceptions](../reference/exceptions.md#highscallerror).

## Write the plan on Concert or MathOpt

Suppose you write the plan for CPLEX through Concert, or for OR-Tools through MathOpt, instead of HiGHS. The sets,
the groups and the tables of data are the same on every backend. These parts differ:

- A `VarTable` is constructed with the solver's own model and type of variable. On Concert, it takes an `IloEnv` and
  `ILOFLOAT`, `ILOINT` or `ILOBOOL`. On MathOpt, it takes a `math_opt::Model` and a flag that is true for integer
  variables.
- On Concert, `sumExpr` takes the `IloEnv` first, and `build()` returns an `IloExpr`. On MathOpt, `build()` returns a
  `math_opt::LinearExpression` for a linear term.
- On Concert, `solution()` takes the `IloCplex` that solved the model. On MathOpt, it takes the `SolveResult`.
- The rows and the objective are the solver's own calls, such as Concert's `model.add` and MathOpt's
  `AddLinearConstraint`.
- On Concert, a table does not end its variables, and you end the `IloExpr` that `build()` returns after you use it.
  On MathOpt, a table's variables belong to the `math_opt::Model`, and you use them only while the model exists.

For every entry of each backend, see [`dreamor::concert`](../reference/dreamor-concert.md) and
[`dreamor::mathopt`](../reference/dreamor-mathopt.md).

## See also

- [dreamor::highs](../reference/dreamor-highs.md): every entry of the HiGHS backend, and how to build a program with
  HiGHS, on WebAssembly too.
- [dreamor::concert](../reference/dreamor-concert.md) and [dreamor::mathopt](../reference/dreamor-mathopt.md): every
  entry of the other two backends.
- [Exceptions](../reference/exceptions.md): what each entry throws.
- [Variable families](variable-families.md), [Constraints and objective](constraints-and-objective.md) and
  [Solutions](solutions.md): the plan that these examples solve, and its values.
- Debug-build checks:
  [one expression's variables in one environment](../reference/debug-build-checks.md#highs_one_environment),
  [a table or an expression read with its own environment](../reference/debug-build-checks.md#highs_expression_of_its_environment).

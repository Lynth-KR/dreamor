<!-- Copied from docs/guide/solutions.md (blob 4fc9e5b0421d), tests/docs/highs/solutions.cpp (blob faddd8871e3f) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Solutions

After HiGHS solves the model, you read the values of its variables: how much each machine makes of each item in each
period, and how much stock is kept. A `SolutionTable` holds these values, one `double` for each row of a variable
family's set. You look a value up by the key of its row, as you look a row up in the set. The table holds copies of
the values, so you can read it after the model and the solver are gone.

The examples solve the [guide's model](index.md) on HiGHS:

- `s` holds the model's sets. `s.produce` holds each machine with each needed item that the machine makes, in each
  period: 15 rows. [Match rows with a set](relational-algebra.md#match-rows-with-a-set) builds it. `s.stock` holds
  each needed item in each period. The needed items are the frame, the plate, the bolt and the nut.
- `quantity` is the bill of materials, a `ParamTable<double, cParent, cChild>`.
- `highs::Plan` is the guide's plan on HiGHS. Its `env` holds the model, `vMake` holds how much a machine makes of an
  item in a period, and `vStock` holds how much of an item is kept at a period's end.
  [Variable families](variable-families.md) makes the families, and
  [Constraints and objective](constraints-and-objective.md) adds the rows and the cost.
- `quiet` holds HiGHS's options, with HiGHS's log turned off.
- `m1` refers to the machine M1, and `frame` to the frame. Customers buy 1 frame in period 1 and 2 frames in each of
  periods 2 and 3.

## Read a family's values after a solve

Suppose you want to know how many frames M1 makes in each period. You need the plan and its solver only for the
solve, so read the values into a table that outlives them. Write a function that solves the plan and returns the
values of `vMake`:

```cpp
dreamor::SolutionTable<cMachine, cItem, cPeriod> solveMake(const Sets& s,
                                                           const BomTable& quantity,
                                                           const HighsOptions& options) {
  highs::Plan plan{s, quantity, options};
  plan.env.highs().run();
  return plan.vMake.solution(plan.env);  // the plan and its solver end here, the values stay
}

```

- `highs::Plan plan{s, quantity, options}` builds the plan: its variable families, its rows and its cost.
- `plan.env.highs()` returns HiGHS's own `Highs` object, which holds the model, and its `run()` solves the model.
- `plan.vMake.solution(plan.env)` returns a `SolutionTable<cMachine, cItem, cPeriod>`, with a copy of the value of
  each variable of `vMake` in the solution that the `Highs` holds. On Concert, `solution` takes the `IloCplex` that
  solved the model, and on MathOpt it takes the `SolveResult`.
- `solveMake` returns the table. The plan and its solver are destroyed when the function returns, and the table
  keeps its values.

Call the function, and read a value by its key:

```cpp
const auto made = solveMake(s, quantity, quiet);

const double frames = made(m1, frame, 2);  // 2, the frames customers buy in period 2
const std::tuple<cMachine, cItem, cPeriod> row{m1, frame, 3};
const double same = made(row);  // 2, read by a row of the set
```

- `made(m1, frame, 2)` returns the value of the row that M1, the frame and period 2 identify.
- `row` is a row of `s.produce`: M1, the frame and period 3. `made(row)` reads the value by the whole row,
  of the type that a loop over the set gives you.

`made` holds a value for each of the 15 rows of `s.produce`, the set of `vMake`. M1 is the only machine that makes
the frame. The plan makes each period's frames in that period: 1 frame in period 1, and 2 frames in each of periods
2 and 3. So `frames` and `same` are both 2.

For the keys that a lookup takes, and the exception for a key that the set does not have, see
[`operator()`](../reference/solutiontable.md#solutiontable-operator-call). For what `solution` throws if HiGHS holds
no feasible solution, see [`solution`](../reference/dreamor-highs.md#vartable-solution), and for what it takes on the
other backends, see Concert's [`solution`](../reference/dreamor-concert.md#vartable-solution) and MathOpt's
[`solution`](../reference/dreamor-mathopt.md#vartable-solution).

## Walk the values in the set's order

Suppose you want the plan as text, with a line for each machine, item and period in which the machine makes
something. `made`, from the section above, holds what the machines make. Walk it with a range-for:

```cpp
std::vector<std::string> lines;  // what the machines make, in the set's order
for (const auto& [row, units] : made) {
  const auto& [m, i, t] = row;
  if (units > 1e-6) { lines.push_back(std::format("{} makes {:g} {} in period {}", m, units, i, t)); }
}
```

- The range-for gives each row of the set with its value, in the order of the set's rows. `[row, units]` splits each
  pair into the row and the value.
- `[m, i, t]` splits the row into the machine, the item and the period.
- The `if` skips the rows on which the plan makes nothing. The tolerance of 1e-6 allows for the solver's
  floating-point values.
- `std::format` writes each `Ref` as its entity's primary key, and `{:g}` writes a whole number without a decimal
  point.

`lines` follows the order of `s.produce`, which lists M1 before M2 and M1's items in the order of M1's `items`, the
frame first. So `lines` starts with M1's frames: `M1 makes 1 frame in period 1`, `M1 makes 2 frame in period 2` and
`M1 makes 2 frame in period 3`. The next line is `M1 makes 6 plate in period 1`. M1 makes no plates in periods 2 and
3, so those rows have no line: the next line is `M2 makes 4 plate in period 3`. The last line is
`M2 makes 13 nut in period 2`.

For the pairs that the range-for gives, see [`begin`](../reference/solutiontable.md#solutiontable-begin).

## Carry a solve into the next model

Suppose you plan again from period 2 on, once period 1 is over. The next plan starts with the stock that this plan
keeps at the end of period 1. A `ParamTable` holds data on a set for a model, so make one from the values of `vStock`.
Here `plan` is a `highs::Plan` that HiGHS has solved:

```cpp
using dreamor::index_set::relational::from;
const auto after_period_1 = [](cPeriod t) { return t == 1; };
const auto sOPENING       = from(s.stock).where(after_period_1).build();  // each item at period 1's end

const auto kept = plan.vStock.solution(plan.env);
const dreamor::ParamTable<double, cItem, cPeriod> opening{sOPENING, kept};  // the next plan's opening stock
```

- `after_period_1` is a column callable, a lambda whose parameters are columns. It is true for period 1.
- `from(s.stock)` starts a chain with the rows of `s.stock` and its columns, `cItem` and `cPeriod`.
  `where(after_period_1)` keeps the rows of period 1, so `sOPENING` holds each needed item at the end of period 1.
- `plan.vStock.solution(plan.env)` returns the stock of each needed item at the end of each period.
- The `ParamTable` constructor calls `kept` with each row of `sOPENING`, and copies the value that `kept` returns. A
  `SolutionTable` is a column callable over its key columns, so the constructor takes it where it takes a callable.

`opening` holds the stock of the frame, the plate, the bolt and the nut at the end of period 1. The plan keeps 4
plates and nothing else: M1 makes 6 plates in period 1, as the section above shows, and the frame made in period 1
takes 2 of them. The code is in `openingStock`, which returns `opening`. `opening` keeps its values after the plan
and its solver are destroyed.

For what the constructor takes, see [`ParamTable`](../reference/paramtable.md#paramtable-constructor), and for a
`SolutionTable` as a column callable, see [SolutionTable](../reference/solutiontable.md).
[Data on a set](data-on-a-set.md) shows how a model reads a `ParamTable`.

## Read one variable or one expression

Sometimes you want one value, not a table: the frames that M1 makes in period 1, or in the three periods together.
`val` returns the value of one variable, or of a linear expression, in the solution that the `Highs` holds. Here
`plan` is a `highs::Plan` that HiGHS has solved:

```cpp
const double first = dreamor::highs::val(plan.env, plan.vMake(m1, frame, 1));  // 1

const auto framesOnM1 = plan.vMake(m1, frame, 1) + plan.vMake(m1, frame, 2) + plan.vMake(m1, frame, 3);
const double all      = dreamor::highs::val(plan.env, framesOnM1);  // 5
```

- `plan.vMake(m1, frame, 1)` returns the variable of M1's frames in period 1, and `val` returns its value.
- `framesOnM1` is a linear expression, the sum of M1's frame variables in the three periods. `val` returns the value
  of the sum.

`first` is 1. `all` is 5, the 1 + 2 + 2 frames that customers buy in the three periods. `val` belongs to the HiGHS
backend. On Concert and MathOpt, read one value through the solver's own API.

For what `val` takes, and what it throws if HiGHS holds no feasible solution, see
[`val`](../reference/dreamor-highs.md#val).

## See also

- [SolutionTable](../reference/solutiontable.md): every member of `SolutionTable`.
- [Variable families](variable-families.md): the families whose values `solution` returns.
- [Data on a set](data-on-a-set.md): a `ParamTable`, which you read as you read a `SolutionTable`.
- [Backends](backends.md): the `HighsEnv` and the `Highs`, and what differs on Concert and MathOpt.
- Debug-build checks:
  [a table or an expression read with its own environment](../reference/debug-build-checks.md#highs_expression_of_its_environment).

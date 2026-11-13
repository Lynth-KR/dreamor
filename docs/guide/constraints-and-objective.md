# Constraints and objective

A constraint is a row of the model: a linear expression of variables between a lower and an upper bound. The model's
rows come in families, such as a row for each machine in each period. dreamor has no table of constraints. You write
a family of rows as a loop over the set that the family runs over, and add one row in each pass of the loop. The
objective is one expression, which the solver minimizes or maximizes.

HiGHS's own C++ API has no expressions: a row is its bounds with arrays of columns and coefficients. dreamor's HiGHS
backend adds the expressions of highspy, the Python interface of HiGHS. You write a row with `<=`, `>=` or `==`, and
`addConstr` adds it to the model. On Concert and on MathOpt, you add a row with the solver's own call, `model.add` or
`model.AddLinearConstraint`; see [`dreamor::concert`](../reference/dreamor-concert.md) and
[`dreamor::mathopt`](../reference/dreamor-mathopt.md).

The examples use the [guide's model](index.md) and its plan on HiGHS. `Plan` holds `env`, the `HighsEnv` that holds
the model, and the variable families `vMake`, `vSetup` and `vStock`, which
[Make the model's families](variable-families.md#make-the-models-families) makes. Its constructor adds the rows and the
cost that this page shows:

```cpp
--8<-- "tests/docs/highs/plan.hpp:plan"
```

The page also uses these names:

- `s` holds the model's sets. `s.PRODUCE` holds each machine with each needed item that the machine makes, in each
  period: 15 rows. [Match rows with a set](relational-algebra.md#match-rows-with-a-set) builds it. `s.STOCK` holds
  each needed item in each period: the frame, the nut, the plate and the bolt, 12 rows.
- `m1` and `frame` are `Ref`s to the machine M1 and the frame. [Entities](entities.md#look-an-entity-up-by-its-key)
  looks them up.

## Add a row for each row of a set

A machine makes an item in a period only if it is set up for the item in that period. For each row `(m, i, t)` of
`s.PRODUCE`, the model needs the row `make(m, i, t) <= max_make(m, i) * setup(m, i, t)`. Without a setup, the machine
makes nothing. With a setup, it makes at most what fits in its hours. Loop over the set, and add a row in each pass:

```cpp
--8<-- "tests/docs/highs/plan.hpp:setups"
```

- `for (const auto& [m, i, t] : s.PRODUCE)` takes each row of `s.PRODUCE`, unpacked into its machine, item and period.
- `vMake(m, i, t) <= max_make(m, i) * vSetup(m, i, t)` makes the row. `max_make`, the upper bound of `vMake`, returns
  the most units of the item that fit in the machine's hours. Both sides hold variables, so the row is
  `make - max_make * setup <= 0`.
- `dreamor::indexedName("Setup", m, i, t)` returns the row's name, made from the family's name and the row's values as
  a `VarTable` names its variables.
- `addConstr(env, row, name)` adds the row to the model of `env`.

The plan has 15 setup rows, one for each row of `s.PRODUCE`. M2 has 3 hours in a period, and a nut takes 0.1 hours, so
M2 makes at most 30 nuts in a period. The row `Setup_M2_nut_3` is `make(M2, nut, 3) - 30 * setup(M2, nut, 3) <= 0`.

For what each call takes, see [`addConstr`](../reference/dreamor-highs.md#addconstr),
[`operator<=`](../reference/dreamor-highs.md#operator-le) and [`indexedName`](../reference/names.md#indexedname). For
how a value is written in a name, see [Names and logging](names-and-logging.md).

## Add a row for each group of rows

Each machine works no more hours in a period than it has. For each machine `m` and period `t`, the model needs the row
`Σ_{i | (m, i, t) ∈ produce} time(i) * make(m, i, t) <= capacity(m)`. The sum runs over one machine's rows of
`s.PRODUCE` in one period, so loop over the groups of those rows:

```cpp
--8<-- "tests/docs/highs/plan.hpp:hours"
```

- `hours` returns the hours of one row: the hours that a unit of the item takes, times the units made.
- `s.PRODUCE.groupBy<cMachine, cPeriod>()` groups the rows of `s.PRODUCE` by their machine and their period. The loop
  unpacks each group into its machine `m`, its period `t` and its `rows`.
- `sumExpr(rows, hours).build() <= m->capacity` makes a row with the machine's hours as its upper bound.

The plan has 6 hours rows, one for each of the 2 machines in each of the 3 periods. M1 makes the frame and the plate,
and the plan does not need the bracket, so `Hours_M1_1` holds two terms, up to M1's 4 hours. `Hours_M2_3` holds M2's
plate, bolt and nut, up to M2's 3 hours.

For how to group a set, see [Grouping](grouping.md). For the sum over a group, see
[Add a term up over a group's rows](sums.md#add-a-term-up-over-a-groups-rows).

## Read the period before where there is one

An item's stock carries from one period to the next. For each row `(i, t)` of `s.STOCK`, the model needs the balance
`stock(i, t - 1) + made(i, t) - taken(i, t) - stock(i, t) = demand(i, t)`. `made(i, t)` is what every machine makes
of `i` in period `t`, and `taken(i, t)` is what the parents made in period `t` take of `i`. Period 1 has no period
before it, so its balance has no stock carried in:

```cpp
--8<-- "tests/docs/highs/plan.hpp:balance"
```

- The two sums make `made(i, t)` and `taken(i, t)`.
  [Add up what the parents take](sums.md#add-up-what-the-parents-take) shows how.
- `s.PERIODS.ordered()` reads the periods in their order. `orderedPeriods.prev(t)` returns a pointer to the period
  before `t`, or a null pointer in period 1.
- `if (const auto* before = ...)` adds `vStock(i, *before)`, the stock carried in, to `inflow` only if there is a
  period before.
- `==` makes a row whose lower and upper bounds are both the demand.

The plan has 12 balance rows, one for each of the 4 needed items in each of the 3 periods. `Balance_frame_1` equals 1,
the frame's demand in period 1, and `Balance_nut_2` equals 5, the nut's demand in period 2. `Balance_plate_1` holds 4
terms: what M1 and M2 make, what M1's frames take and the stock kept. `Balance_plate_2` holds a fifth term, the plates
kept from period 1.

For how to read a set in its order, see [Ordered sets](ordered-sets.md) and
[`prev`](../reference/ordered.md#ordered-prev). For the row that `==` makes, see
[`operator==`](../reference/dreamor-highs.md#operator-eq).

## Grow an expression

The balance declares `inflow` as a `HighsLinearExpression`, and then adds a term to it. Suppose you want the bolts
that M1's frames and plates take in period 1: 4 bolts for each frame and 2 for each plate. Write the expression one
term at a time:

```cpp
--8<-- "tests/docs/highs/constraints_and_objective.cpp:grow"
```

- `quantity(cParent{frame}, cChild{bolt})` reads the quantity of the bill of materials' line from the frame to the
  bolt. `cParent{frame}` and `cChild{bolt}` name the column of each part of the key.
- The first line multiplies the quantity by the variable of M1's frames, and stores the term in `bolts`.
- `+=` adds the term of M1's plates to `bolts`.

`bolts` is `4 * make(M1, frame, 1) + 2 * make(M1, plate, 1)`. A number times a variable is a `HighsLinearTerm`, a
single term that has no `+=`. If `bolts` were declared `auto`, it would be a `HighsLinearTerm`, and the second line
would not compile. Declare the type, `HighsLinearExpression`, for an expression that grows.

For the expression and the term, see [`HighsLinearExpression`](../reference/dreamor-highs.md#highslinearexpression),
[`operator+=`](../reference/dreamor-highs.md#highslinearexpression-operator-plus-eq) and
[`HighsLinearTerm`](../reference/dreamor-highs.md#highslinearterm). For when a key must name its columns, see
[Keys](../reference/keys.md).

## Bound a row on both sides

Suppose M1 must make no fewer frames in period 2 than in period 1, and at most 5 more. The row has both bounds:
`0 <= make(M1, frame, 2) - make(M1, frame, 1) <= 5`. Write it as you would write it on paper:

```cpp
--8<-- "tests/docs/highs/constraints_and_objective.cpp:two-bounds"
```

- `0 <= plan.vMake(m1, frame, 2) - plan.vMake(m1, frame, 1)` makes a row with only a lower bound, a
  `HighsLowerBoundedLinearExpression`.
- `<= 5` adds the upper bound, and returns a `HighsBoundedLinearExpression`.

`ramp` is the row between 0 and 5, with two terms. It is not in the model yet; the next section adds it.

For the two kinds of row, see
[`HighsLowerBoundedLinearExpression`](../reference/dreamor-highs.md#highslowerboundedlinearexpression) and
[`HighsBoundedLinearExpression`](../reference/dreamor-highs.md#highsboundedlinearexpression).

## Change a row through its handle

Suppose you add `ramp`, from the section above, to the model, and later want to narrow it to at most 1 more frame.
HiGHS's own calls change a row by its index, which `addConstr` returns:

```cpp
--8<-- "tests/docs/highs/constraints_and_objective.cpp:handle"
```

- `addConstr` adds `ramp`, named `Ramp_M1_frame_2`, and returns the row's `HighsCons`.
- `rampRow.index()` returns the row's index in HiGHS's model.
- `plan.env.highs()` returns the `Highs`, HiGHS's own solver object, and its `changeRowBounds` sets the row's bounds to
  0 and 1.

The plan has 33 rows: 12 balance rows, 6 hours rows and 15 setup rows. They take the indexes 0 to 32, so
`rampRow.index()` is 33. The row `Ramp_M1_frame_2` now runs from 0 to 1.

For the handle, see [`HighsCons`](../reference/dreamor-highs.md#highscons). For when the model reaches the `Highs`, see
[`highs`](../reference/dreamor-highs.md#highsenv-highs).

## Set the objective

The plan pays for holding stock and for setting up, and the best plan costs least. The objective is to minimize the
cost of holding, `Σ_{(i, t) ∈ stock} hold(i) * stock(i, t)`, plus the cost of setting up,
`Σ_{(m, i, t) ∈ produce} setup_cost(m) * setup(m, i, t)`:

```cpp
--8<-- "tests/docs/highs/plan.hpp:cost"
```

- `holding` returns the cost of one row of `s.STOCK`: the cost of holding a unit of the item for a period, times the
  stock.
- `setups` returns the cost of one row of `s.PRODUCE`: the machine's cost of a setup, times the setup variable.
- Each sum is built, and `+` adds the two expressions.
- `setObjective(env, ..., ObjSense::kMinimize)` sets the expression as the objective, and has HiGHS minimize it.

A setup on M1 costs 10, and a frame kept for a period costs 2. What is made costs nothing. When HiGHS solves the
plan, the least cost is 75.4.

For what an objective may be, see [`setObjective`](../reference/dreamor-highs.md#setobjective).

## Set the objective and solve in one call

Suppose you want, instead, the plan that keeps the least stock, and you want to solve the model at once:

```cpp
--8<-- "tests/docs/highs/constraints_and_objective.cpp:minimize"
```

- `sumExpr(s.STOCK, plan.vStock).build()` adds up every stock variable.
- `minimize` sets the expression as the objective to minimize, runs HiGHS, and returns the status that `run()`
  returns.

`status` is `HighsStatus::kOk`. The new objective replaces the plan's cost, so a setup costs nothing now. The least
stock is 0, because each period's needs fit in that period's hours. In period 2, for example, 2 frames and their 4
plates take M1's 4 hours, and the 16 bolts and 13 nuts that they and the customers need take 2.9 of M2's 3 hours.

For `minimize`, and for `maximize`, which maximizes the objective, see
[`minimize`](../reference/dreamor-highs.md#minimize) and [`maximize`](../reference/dreamor-highs.md#maximize).

## See also

- [`dreamor::highs`](../reference/dreamor-highs.md): the expressions, the rows and what each operator makes.
- [Variable families](variable-families.md): the variables of the rows.
- [Sums](sums.md): the sums that the rows and the objective are made of.
- [Solutions](solutions.md): the values that a solve leaves.
- Debug-build checks:
  [the variables of an expression belong to one environment](../reference/debug-build-checks.md#highs_one_environment),
  [and to the environment that the expression is handed to](../reference/debug-build-checks.md#highs_expression_of_its_environment).

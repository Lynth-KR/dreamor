# Variable families

A model's variables come in families. How much a machine makes of an item in a period, for example, is a family: one
variable for each machine, each item that the machine makes and each period. A `VarTable` holds such a family. It
makes one variable for each row of a set when it is constructed, and you look a variable up by its row's key, as you
look a row up in the set.

The examples use the [guide's model](index.md) on HiGHS:

- `s` holds the model's sets. `s.produce` holds each machine with each needed item that the machine makes, in each
  period: 15 rows. [Match rows with a set](relational-algebra.md#match-rows-with-a-set) builds it. `s.stock` holds
  each needed item in each period: the frame, the nut, the plate and the bolt, 12 rows. `s.periods` holds the periods
  1, 2 and 3. [Set-builder notation](set-builder-notation.md) shows the chains that build the model's sets.
- `m1` and `m2` are `Ref`s to the machines M1 and M2, and `frame`, `plate`, `bolt` and `nut` are `Ref`s to the items.
  [Entities](entities.md#look-an-entity-up-by-its-key) looks them up.
- A `HighsEnv` holds the model that HiGHS solves. [Backends](backends.md) shows its options.

## Make the model's families

The plan decides three things for each row of the model's sets: how much a machine makes of an item in a period,
whether the machine is set up for that item in that period, and how much of an item is kept at the end of a period.
`Plan` holds the environment and one family for each of the three:

```cpp
--8<-- "tests/docs/highs/plan.hpp:plan"
```

- `env` is the `HighsEnv`. It is declared before the tables, because each table adds its variables to it, and C++
  constructs the members in the order of their declaration.
- `vMake{env, s.produce, 0, max_make, HighsVarType::kContinuous, "Make"}` adds a continuous variable for each row of
  `s.produce`. Its lower bound is 0, and its upper bound is what `max_make` returns for the row.
- `max_make` is a column callable: a lambda whose parameters are columns. The constructor of `vMake` calls it on each
  row, with the row's machine and item. `max_make` is a `static constexpr` member, because the initializer list has
  no place to declare a local variable.
- `vSetup` adds an integer variable for each row of `s.produce`, from 0 to 1.
- `vStock` adds a continuous variable for each row of `s.stock`, from 0 to `kHighsInf`, which is no upper bound.
- The three tables are constructed in the initializer list. A `VarTable` has no default constructor, so a class that
  holds one constructs it there.
- The last argument of each table is the family's name. Each variable is named by the family's name and the values of
  its row, joined by `_`.
- The constructor's body adds the plan's rows and its cost, which
  [Constraints and objective](constraints-and-objective.md) shows.

The plan has 42 variables: 15 in `vMake`, 15 in `vSetup` and 12 in `vStock`. M1 has 4 hours in a period. A frame
takes 1 hour, so M1's frames in a period run from 0 to 4, and a plate takes 0.5 hours, so M1's plates run from 0 to 8.
M2 has 3 hours, so M2's plates run from 0 to 6. The variable of M1's frames in period 1 is named `Make_M1_frame_1`.

For what the constructor takes, see [`VarTable`](../reference/dreamor-highs.md#vartable-constructor). For how a
value is written in a name, see [Names and logging](names-and-logging.md). On Concert and on MathOpt, `VarTable` takes
that backend's own model and variable types; see [Concert's `VarTable`](../reference/dreamor-concert.md#vartable-constructor)
and [MathOpt's `VarTable`](../reference/dreamor-mathopt.md#vartable-constructor).

## Take a bound from the data

`max_make`, in the section above, works out each row's upper bound from the row's machine and item. Often the bound
is data that you already hold. Suppose each machine's crew has agreed on the extra hours that the machine may work in
a period: 2 hours on M1 and 1 hour on M2. You want a variable for each machine's overtime in each period, up to the
agreed hours. Hold the hours in a `ParamTable`, and pass the table as the bound. `OvertimeRecord` is this page's own
type, not part of the model. Here `env` is a `HighsEnv` of its own, and `machines` is the model's vector of machines:

```cpp
--8<-- "tests/docs/highs/variable_families.cpp:overtime-record"
```

```cpp
--8<-- "tests/docs/highs/variable_families.cpp:bound-from-data"
```

- `sSHIFTS` pairs each machine with each period of `s.periods`.
- `agreed` holds one record for each machine. `machine_of` looks the record's machine up by its name, and `hours_of`
  returns the record's hours.
- `overtime` is a `ParamTable` on the machines alone: one value for each record, by the record's machine.
- `vOvertime` takes `overtime` as its upper bound. A `ParamTable` is a column callable over its key columns, so the
  constructor calls it on each row of `sSHIFTS` with the row's machine, as it calls `max_make`. The period, which is
  not a column of `overtime`, is left out.

`vOvertime` has a variable for each machine in each period: 2 machines in 3 periods, 6 variables. M1's variables run
from 0 to 2 hours in each period, and M2's from 0 to 1 hour. The table holds the hours once for each machine, and the
family reads them in every period.

For how to fill a table with your own values, see [Data on a set](data-on-a-set.md). For what a bound may be, see
[`VarTable`](../reference/dreamor-highs.md#vartable-constructor).

## Read a family

To write a row of the model by hand, or to check what a family holds, read a variable by its row's key. `plan` is
the model's `Plan`, from the first section:

```cpp
--8<-- "tests/docs/highs/variable_families.cpp:read"
```

- `plan.vMake(m1, frame, 1)` returns the variable of the row with M1, the frame and period 1, as a `HighsVar`. The key
  takes the set's columns in the set's order.
- `frames.index()` returns the variable's column in HiGHS's model. `plan.env.highs()` returns the `Highs`, HiGHS's own
  solver object, and HiGHS's `getLp()` holds the name of each column.
- `plan.vMake.contains(m1, nut, 1)` checks whether the family's set has the row.
- `size()` returns the number of variables, and `keys()` returns the set that the family was made over.

`name` is `Make_M1_frame_1`. M1 makes no nuts, so `s.produce` has no row with M1 and the nut, and `makesNuts` is false.
`plan.vMake(m1, nut, 1)` would throw `std::out_of_range`, because the set has no row with that key. `count` is 15,
one for each row of `s.produce`, and `sMADE` holds the same rows as `s.produce`.

For each member, see [`operator()`](../reference/vartable.md#vartablecore-operator-call),
[`contains`](../reference/vartable.md#vartablecore-contains), [`size`](../reference/vartable.md#vartablecore-size),
[`keys`](../reference/vartable.md#vartablecore-keys), [`HighsVar`](../reference/dreamor-highs.md#highsvar) and
[`highs`](../reference/dreamor-highs.md#highsenv-highs).

## Walk the rows and their variables

To do something with every variable of a family, such as writing each variable's column to a report, walk the family.
Each step of the walk gives a row and its variable:

```cpp
--8<-- "tests/docs/highs/variable_families.cpp:walk"
```

- `for (const auto& [row, stock] : plan.vStock)` takes each row of `s.stock` with its variable, in the order of the
  set.
- `const auto& [i, t] = row;` unpacks the row into its item and its period.
- `std::format` writes the item as its name, and the variable's column as a number.

`columns` holds 12 lines, from `frame in period 1: column 30` to `bolt in period 3: column 41`. The constructor of a
family adds its variables as consecutive columns of the model, in the order of its set. `vMake` and `vSetup` are made
first and take columns 0 to 29, so `vStock` takes columns 30 to 41. `s.stock` holds the items in the order of the
needed items: the frame and the nut, which customers buy, then the plate and the bolt. So the frame's three periods
take columns 30 to 32, the nut's 33 to 35, the plate's 36 to 38 and the bolt's 39 to 41.

For the order of the walk, see [`begin`](../reference/vartable.md#vartablecore-begin).

## Change a bound later

Suppose M2's tooling for plates is away, and you want M2 to make no plates in the model that `Plan` has built. A
`VarTable` makes its variables once, with their bounds. To change a bound later, call HiGHS's own `changeColBounds`
with the variable's column:

```cpp
--8<-- "tests/docs/highs/variable_families.cpp:change-bound"
```

- `lacks_tooling` is true for a row whose machine is M2 and whose item is the plate.
- `s.produce.where(lacks_tooling)` returns the rows of `s.produce` for which `lacks_tooling` is true.
- `plan.vMake(row)` looks the variable up by the whole row, not by its columns one by one.
- `changeColBounds(..., 0, 0)` sets both bounds of the variable's column to 0.
- `run()` solves the model with the new bounds.

The variables of M2's plates in periods 1, 2 and 3 are fixed to 0, and M1's plates still run from 0 to 8. The solve
finds a plan in which M2 makes no plates, so M1 makes every plate.

For what `where` returns, see [`where`](../reference/indexset.md#indexset-where). For when the model reaches the
`Highs`, see [`highs`](../reference/dreamor-highs.md#highsenv-highs) and [Backends](backends.md).

## See also

- [VarTable](../reference/vartable.md): every member, and why a table can be moved but not copied.
- [`dreamor::highs`](../reference/dreamor-highs.md): `HighsEnv`, `HighsVar` and the HiGHS `VarTable`.
- [Sums](sums.md): a family's variables in a sum.
- [Constraints and objective](constraints-and-objective.md): the rows and the cost that `Plan` adds.
- [Solutions](solutions.md): a family's values after a solve.
- [Backends](backends.md): the options of `HighsEnv`, and when the model reaches the `Highs`.

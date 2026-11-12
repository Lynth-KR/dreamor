# Catalog

The catalog measures what each of dreamor's operations costs against the same operation written by hand. It does not
measure one model's build. A whole model's build fixes the operations, the order in which they run and the keys that
they look up, and the [scenarios](../scenario/index.md) measure such builds. The catalog takes the operations one family
at a time instead. Its factors are the shape of the data and the way a model reaches the data, so each result says for
which shape and access pattern it holds.

## The families

| Family | What it measures |
|---|---|
| `lookup` | Finding a row's variable by the row's columns: the row's neighbor in the period before, a family on fewer columns, keys drawn at random, and absent keys. |
| `group` | Grouping rows by the columns that a shape keeps, and one sum over each group. |
| `setop` | Projection, union, intersection, difference and membership of sets of rows. |
| `slice` | One sum for each key of a driver, over the rows of the key's group. |
| `make` | Making the rows, a variable for each row, named or not, and the rows again as a join. |
| `order` | The period before a row's period, a lag of four periods, and a rolling window. |
| `data` | Entities inserted and found by their key, a variable for each, and a solver's values read back. |

Each family sets dreamor against hand-written arms: hash maps, sorted arrays and direct arrays, most of them over a
row's ids packed into one integer, and the usual code that a model writes with `std::map` (`std::set` in `setop`).
They are harsh on purpose: each is hard-coded for its one operation and leaves out what dreamor guarantees - contract
checks, exception safety, generality ([Benchmarks](../index.md)).

## In short

The reference run measured dreamor 0.9.0 on an AMD Ryzen 7 5700X, under GCC 13 and Clang 18 (`results.md`):

- dreamor was faster than the usual code in every family's total, by 1.26 to 12.0 times.
- It was also faster than `best`, the fastest hand-written arm in each repetition and phase of a cell, in every
  family's total: by 1.03 to 1.68 times
  under GCC, and by 1.08 to 3.01 under Clang. Clang's 3.01, in `lookup`, is that high because of a missed
  optimization of Clang's on the hand-written arms' ordinary C++; with those lookups written without it, the bound
  is 1.35 (`results.md`).
- Hand-written code won some phases where it knew the data's structure, such as a position kept beside each entity.

## Try it

```sh
cd bench/catalog
./build.sh
python3 run.py --family lookup --n-levels 100000 --reps 1 --layout off   # a quick trial: 400 runs
python3 report.py ../results/<run>                                       # summary.md in the run's folder
```

A run that a conclusion rests on runs on bare metal, with every repetition under a layout of its own; `running.md`
says how.

## The documents

- [`design.md`](design.md): the families and their phases, the arms, the factors, and why the experiment is designed
  as it is.
- [`running.md`](running.md): building the binaries, running a family, the results folder, reading the report,
  comparing two builds of dreamor, and measuring a new version against earlier runs.
- [`results.md`](results.md): the reference run.
- [`note-hand-lookups.md`](note-hand-lookups.md): what the hand-written lookups waited for under each compiler on the
  reference machine, what the same lookups cost without it, and why the benchmark's code stays as it is.

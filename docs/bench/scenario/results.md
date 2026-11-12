# Scenario results

The reference run of the scenarios. `design.md` describes the models, their phases and the arms that the table
names.

The reference run measured dreamor 0.9.0 on this setup:

- An AMD Ryzen 7 5700X on bare metal, Linux 7.0, boost off and the `performance` governor, pinned to one core.
- GCC 13.3 and Clang 18.1, both on libstdc++ 13, with `-O2 -DNDEBUG -march=native`.
- CPLEX 22.1.2, OR-Tools 9.15 for MathOpt, and the HiGHS 1.12.0 that OR-Tools ships.
- Seed 20261025, `run.py`'s default, and 5 repetitions, each under a memory layout and a code layout of its own.
- **Runs.** dreamor's arm ran on the final code. The other arms' runs come from an earlier run of the same plan, whose
  binaries they share: `run.py --reuse` copied them and ran 30 of them again for each scenario and compiler, as
  controls, which took 0.990 to 1.003 of their copies' time.
- **Tests.** A ratio below differs from 1 when a two-sided t test on its repetitions' log ratios rejects a ratio of 1 at
  the 5% level, after Holm's adjustment over every backend, arm and size of its scenario, for each compiler; p-median at
  L = 100000 is a scenario of its own here. To the solver's start, every ratio differs from 1 but three, which show no
  difference, all under GCC: MathOpt's `hand` on p-median at L = 5000, at 0.999, and CPLEX's `flat` on lot sizing at I =
  170 and 1700, at 1.026 and 1.009.

Every check agreed, and `equivalence.py` found every arm's model the same as dreamor's. Each cell gives dreamor's
time over the arm's, as the geometric mean over the sizes, GCC 13 first and Clang 18 second. Below 1, dreamor is
faster. The measure is the time to the solver's start, the benchmarks' measure:

| Scenario | Arm | CPLEX | MathOpt (HiGHS) | HiGHS | stub (containers alone) |
|---|---|---|---|---|---|
| p-median | hand, positional | 1.028 / 1.034 | 1.005 / 1.017 | 1.060 / 1.079 | 1.121 / 1.135 |
| p-median | a row at a time (`callable-rows`, `calls`) | 1.837 / 1.851 | - | 0.530 / 0.536 | - |
| lot sizing | flat, hand-optimized | 1.017 / 1.034 | 1.015 / 1.032 | 1.086 / 1.107 | 1.199 / 1.228 |
| lot sizing | a row at a time (`callable-rows`) | 2.372 / 2.406 | - | - | - |
| lot sizing | plain, `std::map` | 0.620 / 0.646 | - | - | 0.218 / 0.210 |

- **Through Concert and MathOpt, dreamor builds what hand-written code builds within 5%.** The ratio is 1.01 to 1.05
  through CPLEX and 1.00 to 1.04 through MathOpt, at every size and on both compilers. Against the code that is
  usually written without index sets, dreamor takes 0.55 to 0.71 of the time through CPLEX, and 0.16 to 0.27 on the
  stub.
- **On HiGHS, `dreamor::highs` takes 1.05 to 1.11 times as long as the arrays filled by hand**, which are the lower
  bound. Its model phases take about twice the hand arm's time, 1.7 to 2.1 times, because it builds expressions where
  the hand arm writes the arrays. The rest, `passModel` and HiGHS's own start, takes the same time or less: 0.98 to 1.00
  times the hand arm's. An `addRow` for each row takes nearly twice as long as dreamor: dreamor's ratio to `calls` is
  0.52 to 0.55. MathOpt, the expression API on the same solver, is the rival. `dreamor::highs` reaches HiGHS's start in
  0.29 to 0.35 of MathOpt's time on p-median, and in 0.31 to 0.47 on lot sizing.
- **Against CPLEX's Callable Library, the cost is Concert's.** To the solver's start, dreamor on Concert takes 3.3 to
  3.8 times as long as the matrix arm, and hand-optimized Concert code takes 3.2 to 3.7 times as long.
- **On the stub, the gap to hand-optimized code is the rows that dreamor makes and goes through.** A programmer who
  knows that the rows are a product, locations x customers or routes x periods, computes positions instead of making
  rows. p-median's 5e6 assignment rows cost dreamor about 40 ms, and lot sizing's `variables` phase, which makes the
  production rows and names the variables, takes 1.12 to 1.19 times as long as flat's. Grouping such rows takes 1.2
  to 1.9 times as long as a loop that strides by position: p-median's `assign`, and lot sizing's `constraints` with
  its balance and capacity groups. p-median's `link` and `objective`, a constraint and a term for each assignment row,
  take 1.03 to 1.05 and 1.05 to 1.13 times as long as the hand arm's.
- **At 1e7 rows the picture holds.** At p-median's L = 100000, with 5 repetitions, the ratios are 1.030 / 1.040 on
  CPLEX, 1.004 / 1.015 on MathOpt at 13.2 GiB at the peak, 1.123 / 1.138 on the stub, and 1.078 / 1.098 on HiGHS.

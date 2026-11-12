# Benchmark

For people working on dreamor; `AGENTS.md` says when to read it.

`bench/` is not part of the test suite, and CI never runs it; CI does compile it (`tools/check_bench.py`, every
variant the `build.sh` files build but CPLEX's), so a change to the headers cannot break it unseen. When a change touches what the benchmarks exercise (`AGENTS.md`,
"Before a change", lists it), check with `bench/catalog/build.sh` and `bench/scenario/build.sh`:
- both still build;
- every arm still does the same work: a quick run of each family the change reaches
  (`python3 run.py --family <family> --n-levels 100000 --reps 1 --layout off` in `bench/catalog/`, then
  `report.py`) and of a scenario (`python3 run.py --scenario lotsizing --levels 170 --reps 1 --layout off` in
  `bench/scenario/`, then `report.py`) report no check mismatch. `--layout off` because `build.sh` builds no
  per-repetition copies unless `LAYOUTS` is set.

A timing claim compares builds: the catalog's `run.py --builds base=<old>,head=<new>` and `compare.py`
over the families the change reaches, the whole experiment and not a few cells ("A performance
change" below), and the scenarios for a change a whole model would feel. `python3 -m unittest
common.test_machine` (in `bench/`) tests the CPU choice. An agent's sandbox is not a benchmark
machine.

## A performance change

Optimize for a mechanism, not for the benchmark. The benchmark's arms, factors and data are this repository's own, so
a win there can be a fit to them; what guards against that is the reasoning done before the measurement.

Beneath every rule below is the design of experiments, as the catalog runs it (`docs/bench/catalog/running.md`, "Run a
family"): factors at chosen levels, a fraction of their cells, every arm in every cell, the order shuffled, each
repetition a block, the arms compared in pairs. A claim is a statement about that experiment - which cells moved, by an
interval - not about a run that looked good.

- **Reason first.** Every performance decision needs grounds, and the procedure strongly recommended is to reason from
  them before measuring: what the change does to the work - the instructions, the allocations, the memory it touches -
  and so which phases and cells should get faster, which may get slower, and why. The measurement then confirms or
  refutes that prediction; a result the reasoning did not predict is a question to answer, not a win to keep. The
  grounds are a property dreamor guarantees (a contract, the structure of generation), not how the benchmark draws
  its data, and the change serves an operation real models perform.
- **Show the mechanism.** A change whose effect has not been analyzed with a profiler - perf's counters, or
  callgrind's instruction counts on a machine without counters - is hard to adopt unless its effect is visible beyond
  doubt. The analysis shows that the time moved for the reason given.
- **A doubtful result counts once it repeats.** A result that is not plainly visible - a cell that moved against the
  prediction, a loss or a gain near the noise - counts only when a rerun reproduces it, with another seed or layout;
  until then it is a question, not a finding, whichever way it points.
- **The whole holds, absolutely.** No family's total in the catalog and no scenario - a whole model, through the real
  solver - is slower at any N, by its 95% interval. A cost moved from one phase to another is counted where it lands.
  The catalog's mix of phases is the benchmark's own; a scenario is how a model actually weighs them.
- **A net gain.** Over every cell of the experiment, the geometric mean of the ratios is a gain whose 95% interval
  does not include a ratio of 1.
- **A phase's loss is weighed, not capped.** State it as what one operation costs (ns a call, a row), measured with
  the phase alone (`run.py --phases`), not after what other phases left in the heap, and weigh it against the gain: a
  loss where building is already fast or the shape is uncommon, for a gain where models wait, is the trade a library
  already tuned makes; the commit message states it.
- **Both compilers, other data.** Build every A/B with both compilers at CI's floor, GCC 13 and Clang 18: they inline
  and lay out code differently, and a change can hold under one alone (newer compilers are welcome, not required).
  Check it on data it was not tuned on, another seed or machine.

A phase's loss that the allocator's history makes, not the changed code, is not the change's cost: the phase runs the
same instructions and pays for memory an earlier phase used to leave it - blocks freed by vector doublings the change
no longer makes, or the glibc mmap threshold those frees raised. Show the mechanism (the phase's page faults; the loss
gone or shrunk with `GLIBC_TUNABLES=glibc.malloc.mmap_threshold=33554432`) and state it in the commit message with its
numbers. Instructions tell the two apart: `run.py` counts them where the machine can (`--pmu auto`), and `compare.py`
puts their ratio beside the time's - a loss they follow is the code the compiler made, one they do not is memory.

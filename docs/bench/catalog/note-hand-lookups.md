# A note on the hand-written lookups

Under Clang 18, the hand-written arms of `lookup` that pack keys took 2.1 to 4.8 times their time under GCC 13 in the
reference run, in `hit`, `miss` and `half` alone, while dreamor's arm took the same time under both. This document says
why, what the same lookups cost written another way, what that would change in the results, and why the benchmark's code
stays as it is. `results.md` reports the run; `design.md` describes the arms.

## What the run showed

Over the same cell and repetition, the time under Clang 18 over the time under GCC 13 over the family's total was 3.70 for the
direct array of `lookup`, 1.90 for `identity` and 1.59 for `mixed`, 0.96 for `map`, and 0.99 for dreamor. The instructions of the
hand-written arms moved by 0.93 to 1.19 times, so the time went to waiting, not to more work. `neighbor` moved
little, 0.89 to 1.03; `project` moved by 1.08 to 1.53, and dreamor's by 0.96. Hence `best` over `lookup`'s total was 3.01 under Clang and 1.50 under GCC.

## The code

The hand-written arms find a variable by packing the key's ids into one integer (`bench/catalog/hand/lookup.hpp`):

```cpp
const auto idsOf = [k](const std::array<const Ent*, kMaxColumns>& key) {
  std::array<std::uint32_t, kMaxColumns> id{};
  for (std::size_t j = 0; j < k; ++j) {
    id[j] = key[j]->id;
  }
  return id;
};
...
sum += ubOf(x[ix.find(px(idsOf(key)))]);   // px packs the array: key = key * size[j] + id[j]
```

This is ordinary C++. A small array returned by value and handed straight to a function is what a skilled C++
programmer writes, expecting the compiler to keep the ids in registers.

## What the compilers make of it, on Zen 3

The measurements below ran on the reference machine, an AMD Ryzen 7 5700X (Zen 3), with boost off, the `performance`
governor, and the runner pinned to one core.

- **Both compilers keep the array in memory.** A loop of run-time length fills it, so neither breaks it into
  registers.
- **Clang 18 copies it once more.** In the hand-written binary built with Clang 18 (`clang++-18 -std=c++20 -O2
  -DNDEBUG -march=native`), the array that the lambda filled is copied into the slot that the packer reads, with a
  16-byte load and a 4-byte one (`vmovdqa 0x20(%rsp),%xmm2` and `mov 0x30(%rsp),%r11d`, into `0x140(%rsp)`). The
  16-byte load reads over the 4-byte stores that wrote the ids a few instructions before.
- **On Zen 3, that load cannot take its data from those stores.** The CPU counts a store-to-load interlock
  (`ls_bad_status2.stli_other`, raw event `0x224`), and the load waits until the stores retire. Stores retire in
  order, after every older instruction, so each lookup waits for the one before it, and the lookups no longer
  overlap. `perf annotate` puts 64% of the array's samples on the load of the variable's bound and its conversion
  (`vmovsd 0x8(%r11)`, `vcvttsd2si`).
- **The counts.** In one cell (k = 2, N = 1e6), the array's `hit` took 2,100,000 interlocks for its 1,000,000
  lookups under Clang 18, and 290,000 under GCC 13, which keeps most lookups out of the pattern.
- **Clang 23.1** makes the same copy: 189 ms and 4,090,000 interlocks in the same cell, against 192 ms under Clang 18.
- **`neighbor` and `project`** have the same kind of stack traffic, but their keys come in the order of generation
  and hit the cache, so little sits in front of the stores, and the interlock costs little.
- **Other microarchitectures were not measured.** The hand-written binaries were not run on an Intel CPU. A small
  program with the same lambda and packer, outside the benchmark, did not reproduce the copy: there Clang read the
  ids with 4-byte loads, which forward.

The copy is a missed optimization of Clang's: GCC 13 takes far fewer interlocks on the same source, and Clang 23
still makes the copy.

## The same lookups without the array

Packing the key straight from the entities, with no array in between, keeps the logic and every check the same:

```cpp
const auto packOf = [k, &px](const std::array<const Ent*, kMaxColumns>& key) {
  std::uint64_t packed = 0;
  for (std::size_t j = 0; j < k; ++j) {
    packed = packed * px.size[j] + key[j]->id;
  }
  return packed;
};
...
sum += ubOf(x[ix.find(packOf(key))]);
```

It was measured against the code as written in one cell (owned, dense, skewed, large, shuffled, permuted), at
N = 1e5 and 1e6 and k = 1, 2, 3 and 5 (the array at k = 1 and 2, the only levels where it fits), 3 runs each, paired
by seed, the median. The speed-up over `hit`, `miss` and `half` and the levels of k, as a geometric mean, with the
range in brackets:

| Arm | N | GCC 13 | Clang 18 |
|---|---|---|---|
| array | 1e5 | 1.29 (1.16-1.43) | 5.1 (4.2-6.3) |
| array | 1e6 | 1.29 (1.25-1.33) | 4.7 (3.9-5.7) |
| `identity` | 1e5 | 1.14 (1.09-1.29) | 3.1 (2.0-4.4) |
| `identity` | 1e6 | 1.07 (0.95-1.17) | 3.4 (2.4-4.4) |

At N = 1e5, where the data nearly fits in the 32 MB L3, Clang's array was still five times as slow with the array as
without it: the wait costs even when the lookups hit the cache. Under Clang, `identity`'s speed-up falls as k grows,
since the hashing grows and the wait takes a smaller share of a lookup; the array's rose from k = 1 to k = 2. GCC's arms got faster too: GCC also keeps the array in
memory, at more instructions and fewer interlocks.

## What it would change

To bound the change from dreamor's side, the measured speed-ups were applied to every cell that the code above runs
in, and to no other: the `hit`, `miss` and `half` phases of the arms that pack keys (the array, `identity` and
`mixed`), at the cell's N and k, whatever its other factors. `mixed` packs as `identity` does and was not measured,
so it took `identity`'s speed-ups. `identity` and `mixed` were not measured at k = 8, so they took the speed-ups of
k = 5, which are the larger under Clang, where `identity`'s speed-up falls with k. `map` and dreamor do not run that code, and kept their
times. The ratio of `best` over dreamor, as measured and with the speed-ups:

| Phase | GCC 13 | Clang 18 |
|---|---|---|
| `hit` | 1.22 -> 1.07 | 3.45 -> 0.94 |
| `miss` | 1.69 -> 1.46 | 3.97 -> 1.50 |
| `half` | 1.07 -> 0.97 | 2.66 -> 0.95 |
| **total** | **1.50 -> 1.39** | **3.01 -> 1.35** |

- **The family's total keeps its direction on both compilers:** dreamor stays ahead, at 1.39 and 1.35.
- **`hit` and `half` come to about 1**, at 0.94 to 1.07: written without the array, the best hand-written arm
  would be about level with dreamor on lookups of keys drawn at random.
- **At k = 1 and 2,** where the direct array fits, the best arm would be ahead on `hit`, `miss` and `half`, at 0.57 to
  0.91 under GCC and 0.65 to 0.78 under Clang, but for `miss` at k = 1 under Clang, at 1.08. On the total, it would
  be ahead at k = 1, at 0.75 / 0.86, and behind at k = 2, at 1.11 / 1.17. At k = 3 and above, dreamor would stay
  ahead on the total, at 1.48 to 2.27.

## Why the benchmark's code stays as it is

- **The hand-written arms are ordinary C++.** Knowing that a loop of run-time length keeps a small array in memory,
  and that one compiler then copies it with a load wider than the stores on one family of CPUs, is knowledge of
  compilers and microarchitectures, not of C++. A realistic senior C++ programmer does not write against it, and the
  benchmark's opponent should not either.
- **A faster form is not a defect.** Every hand-written arm has a faster version: prefetching, SIMD, assembly. That
  GCC also runs the form without the array faster names no defect in the form as written.
- **Choosing among equally ordinary forms the one that a compiler happens to compile better has no end.** It would
  mean cutting the hand-written code to each compiler, and to each family of Intel and AMD CPUs, and doing it again at
  each release of either. The benchmark compares dreamor with the realistic ends of hand-written code, the usual
  `std::map` code and the best of the usual alternatives, not with every form that is logically possible.
- **So the lookup family is not measured again, and the tables report the code as written,** each compiler apart,
  with this document beside them. The bound above keeps the direction of every family's total; the `hit` and `half`
  of `lookup` and its cells at k = 1 and 2 are where the reader should weigh it.

## Reproduce it

In `bench/catalog/`, inside the same toolchain as the reference run (Ubuntu 24.04's GCC 13 and Clang 18):

```sh
F='-std=c++20 -O2 -DNDEBUG -march=native -I../common/stub'
g++-13 $F hand.cpp -o hand_g13
clang++-18 $F hand.cpp -o hand_c18
A='--family lookup --impl array --k 2 --N 1000000 --O owned --D dense --S skewed --G large --L shuffled
   --I permuted --rep 2 --seed 20261027 --lookups 1000000 --phases hit --pmu instructions,cycles,stli=0x224'
taskset -c 3 ./hand_c18 $A
taskset -c 3 ./hand_g13 $A
```

`--pmu name=0x...` counts a raw event; `0x224` is Zen 3's `ls_bad_status2.stli_other`. Another CPU names its
store-forwarding events otherwise, and the addresses above hold for the build they come from.

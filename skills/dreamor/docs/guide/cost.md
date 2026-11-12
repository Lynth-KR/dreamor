<!-- Copied from docs/guide/cost.md (blob 0df24c30635f) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Cost

What a model built with dreamor costs beyond its solve: the time to write a large model, and what a set costs to
compile.

## Large models

Writing a model of millions of nonzeros spends much of its time in page faults; transparent huge pages, where Linux
offers them (`GLIBC_TUNABLES=glibc.malloc.hugetlb=1`), take many of those at once. dreamor's figures leave them off.

## Compile cost

dreamor is templates, so what a set costs is partly paid at compile time. One translation unit
(`-std=c++20 -O2 -c`, CPU time and peak memory; a debug build keeps the contract checks, which `-DNDEBUG` compiles
out), measured by `python3 tools/measure_compile_cost.py`, with and without `-DNDEBUG`, on an AMD Ryzen 7 5700X with
boost off, GCC 13.3 and Clang 18.1 on libstdc++ 13.

One set of N columns, filtered, grouped, with a `VarTable` on it and a sum over it:

| Columns | g++ 13 | g++ 13, NDEBUG | clang++ 18 | clang++ 18, NDEBUG |
|---|---|---|---|---|
| 4 | 4.1 s, 323 MB | 3.7 s, 318 MB | 4.1 s, 230 MB | 3.7 s, 222 MB |
| 16 | 4.6 s, 351 MB | 4.2 s, 327 MB | 4.9 s, 259 MB | 4.5 s, 252 MB |
| 32 | 5.6 s, 400 MB | 5.3 s, 388 MB | 6.3 s, 317 MB | 5.9 s, 309 MB |
| 64 | 8.4 s, 547 MB | 8.0 s, 532 MB | 11.2 s, 497 MB | 10.7 s, 486 MB |

N sets of three columns of their own, each with a predicate, a `VarTable` and a sum (`--many`), as a model has
many sets:

| Sets | g++ 13 | g++ 13, NDEBUG | clang++ 18 | clang++ 18, NDEBUG |
|---|---|---|---|---|
| 1 | 3.7 s, 322 MB | 3.4 s, 303 MB | 3.8 s, 222 MB | 3.5 s, 216 MB |
| 50 | 25.8 s, 1359 MB | 23.5 s, 1353 MB | 28.1 s, 927 MB | 25.7 s, 901 MB |
| 200 | 96.6 s, 4830 MB | 88.0 s, 4495 MB | 109.1 s, 3062 MB | 100.6 s, 2963 MB |

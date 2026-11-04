# Continuous integration

What runs on every pull request and push to main, where, and why - and the local run that stands for it before a
push. For people working on dreamor; using it needs none of this.

Two files hold the jobs and say the same thing: `.github/workflows/ci.yml` (GitHub Actions) and `tools/ci_local.sh`
(the same jobs on one machine). Every job calls `tests/run_tests.sh`; its variables pick the compiler and the build.

## The jobs

Lint runs first; when it passes, the rest run at once.

| Job | Machine | What it runs | compile_fail |
|---|---|---|---|
| lint | ubuntu-24.04 | `tools/format.sh --check`, then `tools/tidy.sh` (with OR-Tools' headers, for the MathOpt and HiGHS backends) | - |
| stub, GCC 13 `-O3 -DNDEBUG` | the runner | unit tests, documented code, compile_fail, syntax checks; installs the CMake package and builds `tests/cmake_consumer` with GCC 13 and Clang 18 | yes |
| stub, Clang 18 `-O1 -g` | the runner | the compiler's checks alone (`UNIT_TESTS=0`): compile_fail, syntax checks, documented code | yes |
| stub, Clang 18 `-O3 -DNDEBUG` | the runner | unit tests | - |
| sanitize, GCC 13 and Clang 18 (`SAN=1`) | the runner | AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer: unit tests, contract_fail | - |
| sanitize, GCC 13 (`SAN=thread`) | the runner | ThreadSanitizer: unit tests | - |
| gcc-latest | the `gcc:16` image | stub mode, everything | yes |
| clang-latest | the runner, Clang 23 from apt.llvm.org | stub mode, everything | yes |
| libcxx, 18 and 23 | the runner, Clang and libc++ of one LLVM version | `STDLIB=libc++`: unit tests, contract_fail | - |
| wasm | the `emscripten/emsdk:6.0.13` image | `WASM=1`: stub mode built for wasm32, run under Node.js, everything | yes |
| wasm-highs | the same image, HiGHS built by its Emscripten | `WASM=1 --highs`: the HiGHS backend's tests, example, compile_fail, contract_fail | yes |
| mathopt, GCC 13 and Clang 18 | the runner, the OR-Tools 9.15 C++ prebuilt | `--mathopt` (tests, example, the ported samples, compile_fail), then `--highs` on the HiGHS it ships (tests, example, compile_fail, contract_fail); builds `tests/cmake_consumer_mathopt` | yes |

The compile_fail cases, the syntax checks and the documented code depend on the compiler alone, so each compiler
runs them once; the other jobs set `COMPILE_CHECKS=0`. contract_fail runs in debug builds only (`-O1 -g`), where the
checks exist. CI runs on pushes to main and on pull requests, not on a change of Markdown alone; a work branch runs it
by hand (`gh workflow run tests --ref <branch>`).

## What the jobs cover

Each axis is tested at its two ends and nothing in between, not every combination of axes.

| Axis | One end | The other end | Beside |
|---|---|---|---|
| GCC | 13: stub, sanitize, mathopt | 16: gcc-latest | |
| Clang | 18: stub, sanitize, mathopt, libcxx | 23: clang-latest, libcxx | |
| Standard library | libstdc++ 13, libc++ 18 | libstdc++ 16, libc++ 23 | the MSVC STL, by hand on Windows |
| Platform | Linux x86-64: every job | | wasm32: wasm, wasm-highs; Windows with clang-cl: by hand, `AGENTS.md` "Platforms" (cl.exe being brought up, not yet checked); macOS: none |
| Build | `-O1 -g`, with the debug checks | `-O3 -DNDEBUG` | three sanitizers |

The backends: Concert against the stub in every stub-mode job, MathOpt and HiGHS on the OR-Tools prebuilt in the
mathopt job, HiGHS on wasm32 in wasm-highs; real CPLEX is not on CI (`tests/run_tests.sh --cplex`, by hand).
Performance is measured on Linux x86-64 with libstdc++ alone, and not on CI (`bench/README.md`).

## Images and caches

| What | Used by | Where it comes from | Built again |
|---|---|---|---|
| `docker.io/library/gcc:16` | gcc-latest | the official image | never: pulled once (`podman pull`) |
| `docker.io/emscripten/emsdk:6.0.13` | wasm | the official image | never: pulled once |
| libc++, the newest | libcxx (latest), locally | `tools/containers/libcxx-latest.Containerfile`: Ubuntu 24.04 with Clang and libc++ from apt.llvm.org | when the file changes |
| emsdk with HiGHS | wasm-highs, locally | `tools/containers/highs-wasm.Containerfile`, running `tools/containers/build-highs-wasm.sh` | when either file changes |
| HiGHS for wasm | wasm-highs, on CI | `build-highs-wasm.sh` into `/opt/highs`, kept by `actions/cache` | when the script changes |

On CI each job starts on a fresh runner, which installs what the job needs (Clang 18 or 23, libc++). Locally two
versions of libc++ cannot share a machine - their packages conflict - so libc++ 18 is the machine's and the newest is
an image. `tools/ci_local.sh` builds its images itself, from the commit it tests, and names each by the content of the
files it is built from: a change to one of them builds that image again on the next run (minutes); otherwise the image
is reused.

HiGHS on wasm32 needs a stack of 8 MB (`-sSTACK_SIZE=8MB`, workers' 2 MB), which `WASM=1` links: its simplex and MIP
overflow Emscripten's default of 64 KB, which nothing checks.

## Bumping

- **The compilers' ceiling**, at each major release - GCC every spring, LLVM every March and September: gcc-latest's
  image (`ci.yml`, `tools/ci_local.sh`), clang-latest's version (`ci.yml`; locally the machine's `clang++-<n>`), and
  with LLVM the libcxx job's newest version (`ci.yml`'s matrix, `LLVM_VERSION` in
  `tools/containers/libcxx-latest.Containerfile`). The pinned clang-format and clang-tidy move with LLVM
  (`AGENTS.md`, "Formatting"). A new warning or a changed first error there is what users of that compiler will see:
  fix it, then bump.
- **The floor** (GCC 13, Clang 18, libc++ 18) is a decision, not a cleanup.
- **Emscripten** has no long-term release, so one version is pinned (`ci.yml`, `tools/ci_local.sh`,
  `tools/containers/highs-wasm.Containerfile`); moving it is a tool update, not a bound of support. dreamor needs one
  whose LLVM and libc++ are 18 or later.
- **HiGHS for wasm** is the HiGHS the OR-Tools prebuilt ships (`HIGHS_VERSION` in
  `tools/containers/build-highs-wasm.sh`): it moves with OR-Tools.

## Running it locally

`tools/ci_local.sh [REV]` runs every job over one commit (default `HEAD`) checked out in a temporary worktree, so it
tests what a push publishes and not the working tree. It checks first for everything CI installs - GCC 13, Clang 18 and
23, libc++ 18 (`libc++-18-dev`, `libc++abi-18-dev`, `libunwind-18-dev`), podman with the two official images, cmake,
OR-Tools at `$ORTOOLS_DIR`, uv - and runs nothing if one is missing. Format and tidy run first, and nothing else if
either fails; then the jobs run `CI_JOBS` at a time (default 4), each compiling `JOBS` at a time (default half the
cores). Lower both where memory is short: each compiler takes up to about 1 GB.

A pass is recorded by the commit's tree (`ci-passed/` under the git directory), and `tools/hooks/pre-push` stops a push
to main whose tree has none: it looks the record up rather than running the jobs, and a commit reworded or signed again
keeps its pass. A push of Markdown alone needs no record, as CI does not run for one. Turn the hook on per clone with
`git config core.hooksPath tools/hooks`.

On an AMD Ryzen 7 5700X (16 threads, bare metal, Linux, the defaults `CI_JOBS=4 JOBS=8`) the whole run takes about
6 minutes, a minute more when an image is built. The longest jobs are the sanitizers (GCC 13's `SAN=1` about 2
minutes), tidy, clang-latest, libcxx and wasm (each 1 to 1.5 minutes); wasm-highs takes under half a minute once
HiGHS is built.

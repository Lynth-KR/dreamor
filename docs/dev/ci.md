# Continuous integration

What runs on every pull request and push to main, and on each release tag, where, and why - and the local run that
stands for it before a push. For people working on dreamor; using it needs none of this.

Each job is a workflow preset of `CMakePresets.json` - configure, build and test, `cmake --workflow --preset <name>`:
the tests are `tests/CMakeLists.txt`'s, run by CTest. Two files run the jobs and say the same thing:
`.github/workflows/ci.yml` (GitHub Actions, the presets plus what each runner installs) and `ci/local.py` (the same
jobs on one machine, but windows and macos, which need their own systems, and coverage, whose upload is GitHub's).

## The jobs

Every job starts at once, lint among them.

| Job | Preset | Machine | What it runs | compile_fail |
|---|---|---|---|---|
| lint | - | ubuntu-24.04 | `tools/format.sh --check`, `tools/check_layers.py` (the headers keep their layers), `tools/gen_reference.py --check docs/reference` (the API reference's pages are what the headers' `///` comments make), `tools/sync_skill_docs.py --check` (the skill's copies of the guide and the reference are the pages'), `tools/gen_arity_tests.py --check` (the generated compile_fail cases are what the script writes), `tools/check_bench.py` (the benchmarks compile, every variant their `build.sh` builds but CPLEX's; they are never run), `tools/site.sh` (the documentation site builds: each guide page's regions are in its test), then `tools/tidy.sh` (with OR-Tools' headers, for the MathOpt and HiGHS backends) | - |
| stub, GCC 13 Release | `stub-gcc13-release` | the runner | unit and stress tests, documented code, compile_fail, syntax checks; installs the CMake package and builds `tests/cmake_consumer` with GCC 13 and Clang 18 | yes |
| stub, Clang 18 `-O1 -g` | `stub-clang18-debug` | the runner | the compiler's checks alone: compile_fail, syntax checks, documented code | yes |
| stub, Clang 18 Release | `stub-clang18-release` | the runner | unit and stress tests | - |
| sanitize, GCC 13 and Clang 18 | `sanitize-gcc13`, `sanitize-clang18` | the runner | AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer: unit and stress tests, contract_fail | - |
| sanitize, GCC 13 thread | `sanitize-thread-gcc13` | the runner | ThreadSanitizer: unit and stress tests | - |
| gcc-latest | `gcc-latest` | the `gcc:16` image, with Debian's CMake | stub mode, everything | yes |
| clang-latest | `clang-latest` | the runner, Clang 23 from apt.llvm.org | stub mode, everything | yes |
| libcxx, 18 and 23 | `libcxx-18`, `libcxx-latest` | the runner, Clang and libc++ of one LLVM version | unit and stress tests, contract_fail | - |
| wasm | `wasm` | the `emscripten/emsdk:6.0.13` image | stub mode built for wasm32, run under Node.js, everything | yes |
| wasm-highs | `wasm-highs` | the same image, HiGHS built by its Emscripten | the HiGHS backend's tests, example, compile_fail, contract_fail | yes |
| mathopt, GCC 13 and Clang 18 | `mathopt-gcc13`, `mathopt-clang18` | the runner, the OR-Tools 9.15 C++ prebuilt | the MathOpt backend (tests, example, the ported samples, compile_fail) and the HiGHS backend on the HiGHS it ships (tests, example, compile_fail, contract_fail); builds `tests/cmake_consumer_mathopt` | yes |
| coverage | `coverage` | the runner, Clang 18, the OR-Tools 9.15 C++ prebuilt | stub mode and the MathOpt and HiGHS backends with Clang's source-based coverage: unit, stress, docs, examples, contract_fail; `tools/coverage/lcov.py`'s lcov files uploaded to Codecov (`testing.md`, "Coverage"); not run locally | - |
| windows, cl.exe and clang-cl | `windows-cl`, `windows-clang-cl`; `windows-vs2022-cl`, `windows-vs2022-clang-cl` | `windows-2025`, Visual Studio 2026; `windows-2022`, Visual Studio 2022; the OR-Tools prebuilt for Visual Studio 2022 | stub mode at `/W4 /WX`, `/O1` with the debug checks, everything, and the MathOpt and HiGHS backends (tests, examples, samples, the README's program, compile_fail, contract_fail); installs the CMake package and builds `tests/cmake_consumer`, and `tests/cmake_consumer_mathopt` with cl.exe; not run locally | yes |
| macos | `macos` | `macos-15`, Apple Clang, HiGHS from Homebrew, the OR-Tools prebuilt for arm64 macOS | stub mode, everything, the HiGHS backend and the MathOpt backend (tests, examples, samples, compile_fail, contract_fail); installs the CMake package and builds `tests/cmake_consumer` and `tests/cmake_consumer_mathopt`; not run locally | yes |

The compile_fail cases, the syntax checks and the documented code depend on the compiler alone, so each compiler
runs them once; the other presets leave them out (`DREAMOR_TESTS`). contract_fail runs in debug builds only
(`-O1 -g`), where the checks exist. Release is CMake's, `-O3 -DNDEBUG`. CI runs on pushes to main and on pull
requests, not on a change of Markdown alone - but for the README, whose program the HiGHS jobs take out and run,
and the site's pages under `docs/` (not `docs/dev/`), whose regions and links lint's site build checks; a work branch runs it by hand (`gh workflow run tests --ref <branch>`).

## What the jobs cover

Each axis is tested at its two ends and nothing in between, not every combination of axes. The floor is Ubuntu 24.04 as it comes: on
Linux every Clang build and clang-tidy read GCC 13's libstdc++ (`--gcc-install-dir`, in the Clang presets and
`tools/tidy.sh`), whatever other GCC a runner or a machine has - GitHub's has 12 to 14, and Clang picks the newest.

| Axis | One end | The other end | Beside |
|---|---|---|---|
| GCC | 13: stub, sanitize, mathopt | 16: gcc-latest | |
| Clang | 18: stub, sanitize, mathopt, libcxx | 23: clang-latest, libcxx | |
| Standard library | libstdc++ 13, libc++ 18 | libstdc++ 16, libc++ 23 | the MSVC STL: windows; Apple's libc++: macos |
| Platform | Linux x86-64: every job | | wasm32: wasm, wasm-highs; Windows, cl.exe and clang-cl of Visual Studio 2022 and 2026: windows; macOS, Apple Clang: macos |
| Build | `-O1 -g`, with the debug checks | `-O3 -DNDEBUG` | three sanitizers |

The backends: Concert against the stub in every stub-mode job; MathOpt and HiGHS on the OR-Tools prebuilt of the
platform in mathopt, coverage and windows; in macos MathOpt on that prebuilt and HiGHS from Homebrew; HiGHS on wasm32
in wasm-highs. Real CPLEX is not on CI (the `cplex` preset, by hand).
Performance is measured on Linux x86-64 with libstdc++ alone, and not on CI (`docs/bench/index.md`).

## Images and caches

| What | Used by | Where it comes from | Built again |
|---|---|---|---|
| `docker.io/library/gcc:16` | gcc-latest | the official image; CI installs CMake in a step, locally `ci/containers/gcc-latest.Containerfile` adds it | never: pulled once (`podman pull`); the local image when its file changes |
| `docker.io/emscripten/emsdk:6.0.13` | wasm | the official image | never: pulled once |
| libc++, the newest | libcxx-latest, locally | `ci/containers/libcxx-latest.Containerfile`: Ubuntu 24.04 with Clang and libc++ from apt.llvm.org | when the file changes |
| emsdk with HiGHS | wasm-highs, locally | `ci/containers/highs-wasm.Containerfile`, running `ci/containers/build-highs-wasm.sh` | when either file changes |
| HiGHS for wasm | wasm-highs, on CI | `build-highs-wasm.sh` into `/opt/highs`, kept by `actions/cache` | when the script changes |

On CI each job starts on a fresh runner, which installs what the job needs: Clang 18 or 23, libc++, the OR-Tools
prebuilt of its platform (Ubuntu 24.04, Visual Studio 2022, arm64 macOS), HiGHS from Homebrew. Locally two
versions of libc++ cannot share a machine - their packages conflict - so libc++ 18 is the machine's and the newest is
an image. `ci/local.py` builds its images itself, from the commit it tests, and names each by the content of the
files it is built from: a change to one of them builds that image again on the next run (minutes); otherwise the image
is reused.

HiGHS on wasm32 needs a stack of 8 MB (`-sSTACK_SIZE=8MB`, workers' 2 MB), which the wasm presets link: its simplex
and MIP overflow Emscripten's default of 64 KB, which nothing checks.

## Bumping

- **The compilers' ceiling**, at each major release - GCC every spring, LLVM every March and September: gcc-latest's
  image (`ci.yml`, `ci/local.py`, `ci/containers/gcc-latest.Containerfile`), clang-latest's version (`ci.yml`, the
  `clang-latest` preset), and with LLVM the libcxx job's newest version (`ci.yml`'s matrix, `LLVM_VERSION` in
  `ci/containers/libcxx-latest.Containerfile`). The pinned clang-format and clang-tidy move with LLVM
  (`code-style.md`, "Tools"). A new warning or a changed first error there is what users of that compiler will see:
  fix it, then bump.
- **The floor** (GCC 13, Clang 18, libc++ 18) is a decision, not a cleanup.
- **Emscripten** has no long-term release, so one version is pinned (`ci.yml`, `ci/local.py`,
  `ci/containers/highs-wasm.Containerfile`); moving it is a tool update, not a bound of support. dreamor needs one
  whose LLVM and libc++ are 18 or later.
- **Zensical**, the site's generator, is pinned (`ZENSICAL_VERSION` in `tools/site.sh`) as the formatters are: its
  output and its warnings differ between versions. Bump it on its own, and read a guide page and a reference page
  built with it.
- **HiGHS for wasm** is the HiGHS the OR-Tools prebuilt ships (`HIGHS_VERSION` in
  `ci/containers/build-highs-wasm.sh`): it moves with OR-Tools.

## The site

`.github/workflows/site.yml` publishes the documentation site on GitHub Pages, at https://lynth-kr.github.io/dreamor/
(`site_url` in `zensical.toml`). It runs when a tag `v*` is pushed, not on main, so the site shows the latest release
and a change to the guide reaches it with the next release. It builds the site from the tag with `tools/site.sh`,
which lint's site build has already run on main, and replaces the whole site with it. A run started by hand,
`gh workflow run site -f tag=v0.9.0`, publishes the tag it names: it retries a failed run, and given an older tag it
puts that version up. `ci/local.py` publishes nothing; its part is lint's site build.

The repository needs two settings, made once:

- Settings > Pages > Build and deployment: the source is "GitHub Actions".
- Settings > Environments > `github-pages` > Deployment branches and tags: a rule for the tags `v*`. By default the
  environment lets only the default branch deploy, and a run on a tag fails without the rule.

## Running it locally

`ci/local.py [REV]` runs every job but windows, macos and coverage over one commit (default `HEAD`) checked out in a temporary worktree, so it
tests what a push publishes and not the working tree. It checks first for everything CI installs - GCC 13, Clang 18 and
23, libc++ 18 (`libc++-18-dev`, `libc++abi-18-dev`, `libunwind-18-dev`), podman with the two official images, cmake,
OR-Tools at `$ORTOOLS_DIR`, uv - and runs nothing if one is missing. Lint's quick checks (format, the layers, the
reference, the skill's copies, the generated cases, the site, the benchmarks' compile) run first, with the CMake package
installed - seconds, and nothing else if one fails. Then tidy and the other jobs run `CI_JOBS` at a time (default 4),
tidy first, each building and testing `JOBS` at a time (default the cores shared among `CI_JOBS`, so that the jobs
together run as many processes as there are cores). The first job that fails stops the run: the jobs running are
stopped, the ones waiting never start. Lower both where memory is short: each compiler takes up to about 1.5 GB. The logs go to a temporary folder, kept if a job fails, or to
`--logs DIR`, kept.

The windows and macos jobs run on GitHub's runners alone: a pull request runs them, and a work branch by hand (`gh
workflow run tests --ref <branch>`). What goes to main passes them, as every other job.

A pass is recorded by the commit's tree (`ci-passed/` under the git directory), and `ci/hooks/pre-push` stops a push
to main whose tree has none: it looks the record up rather than running the jobs, and a commit reworded or signed again
keeps its pass. A push of Markdown alone needs no record, as CI does not run for one - but for the README and the
site's pages under `docs/`. Turn the hook on per clone with
`git config core.hooksPath ci/hooks`.

On an AMD Ryzen 7 5700X (16 threads, bare metal, Linux, the defaults `CI_JOBS=4 JOBS=4`) the whole run takes about
18 minutes, tidy the longest job at about 12; a minute more when an image is built.

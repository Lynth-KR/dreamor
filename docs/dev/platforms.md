# Platforms

For people working on dreamor; `AGENTS.md` says when to read it.

Performance is measured, tuned and promised on Linux x86-64 with libstdc++ alone - built by GCC and by Clang, both on
libstdc++: the benchmarks, the rules for "A performance change" (`benchmark.md`), and every figure in the docs are that
platform's. Elsewhere dreamor builds and passes its tests, and is not timed: on LLVM's libc++; on Windows with the
cl.exe and the clang-cl of Visual Studio 2022 17.14 and 2026, on the MSVC STL and the UCRT; on macOS with Apple Clang 17
(Xcode 16.4) on Apple's libc++; and on WebAssembly (wasm32) with Emscripten. What is claimed is what CI's runners
build and test (Checking, below): a version or a part that no job runs - an older Visual Studio, Concert on CPLEX outside
Linux - is not claimed.

- **Portable code over one platform's fast path.** What one instruction set alone has - SIMD (`<immintrin.h>`,
  `_mm_*`, AVX2), BMI, target attributes, inline assembly - is not used: it splits an algorithm by CPU and widens the
  gap between platforms. An operation every 64-bit CPU has, which compilers only spell differently, is fine in one
  helper with a fallback in standard C++ that gives the same result: `internal::foldedMultiply` takes the high
  half of a 64x64 product as `__int128` (GCC, Clang) or `__umulh` (MSVC, `<intrin.h>`), and in 32-bit halves
  elsewhere (`foldedMultiplyPortable`, which `tests/unit/test_tuple_hash.cpp` holds against the others);
  `internal::unreachable()`; `FlatIndex::prefetchForWrite` (no prefetch where there is no builtin).
- **The standard library's common ground.** Use what every supported implementation has at its floor - libstdc++ 13,
  libc++ 18, the MSVC STL of Visual Studio 2022 - and nothing that one of them adds or lacks. Where a feature outside
  that ground has one inside it that does the same at the same speed, take the inside one:
  - `std::thread` and `join()`, not `std::jthread`, which libc++ 18 has only behind `-fexperimental-library`;
  - a `std::formatter` whose `format` is a template on its context, not one taking `std::format_context&`, which
    libc++ 18's `formattable` rejects (it checks with a context of its own).
- **Platform differences live behind macros, one small helper per difference.** The helper holds the
  `#ifdef _WIN32` / `#else` / `#endif` and its callers do not branch: `isTerminal()`, `isEnvSet()` and `openFile()`
  in `logger.hpp` are the pattern. No `#ifdef` inside a function's logic, and no second implementation
  of an algorithm under a macro.
- **Choose the macro by what differs.**
  - The operating system or C runtime (POSIX against Win32/UCRT functions, `<unistd.h>` against `<io.h>`): `_WIN32`.
  - The compiler (builtins, pragmas, attributes): `__clang__` and `__GNUC__`, tested before `_MSC_VER`, because
    clang-cl defines `_MSC_VER` too; cl.exe is the `_MSC_VER` branch (`internal/base/unreachable.hpp`,
    `internal/base/inline.hpp`, `internal/base/third_party_headers.hpp`). An attribute is one of these too: cl.exe warns about a
    `gnu::` attribute at every warning level (C5030), so `DREAMOR_INLINE` and `DREAMOR_NOINLINE` stand for
    `gnu::always_inline` and `gnu::noinline`, and `DREAMOR_LIFETIMEBOUND` (`internal/base/lifetimebound.hpp`) for
    `clang::lifetimebound`, which only Clang has and the others leave empty. A `#pragma GCC` or `#pragma clang` outside such a helper is an unknown
    pragma to cl.exe (C4068), in a test too: `tests/support/float_compare.hpp` lets `std::equal_to` do its `==` instead.
  - The standard library: no macro. Write code that holds on every implementation.
- **Rely on the standard, not on libstdc++'s or libc++'s choices.** What breaks under the MSVC STL:
  - an iterator assumed to be a pointer: `std::string_view::begin()` returns a class there, so name the type
    (`std::format_parse_context::iterator`), never `const auto*` - nor bare `auto`, which `readability-qualified-auto`
    turns back into `const auto*` where the iterator is a pointer;
  - `std::filesystem::path::c_str()` is `const wchar_t*`; pass `path.string().c_str()` to a `const char*`;
  - `long` is 32 bits (LLP64), so `long` and `IloInt` (`__int64`) are different types and a `long` times a Concert
    variable is ambiguous - one more reason for the explicit-size types in `include/` (`code-style.md`, "Library code").
- **cl.exe's preprocessor.** Its default, the traditional one, has no `__VA_OPT__` (`/Zc:preprocessor` is the
  user's flag to set, not dreamor's): a variadic macro takes `...` alone and passes `__VA_ARGS__` on whole, as the
  `DREAMOR_LOG_*` macros do with their format string.
- **Code files are ASCII, comments included.** cl.exe reads a file without a byte-order mark in the system's code
  page unless the user passes `/utf-8`: on a Korean Windows (CP949) a UTF-8 character is warning C4819 in every
  build that includes the header. Write math in words (`union`, `intersection`, `sum over LINKS`).
  `tools/format.sh` names every line of a code file that is not ASCII, and fails, in CI's lint too.
- **A 32-bit `std::size_t` (wasm32).** A value that needs 64 bits says `std::uint64_t`, not `std::size_t`. A key
  of integers, enums or columns of them hashes the same on every platform: `hashElement` hands each element over
  in 64 bits (a column through its own overload, found by argument-dependent lookup), `TupleHash` keeps a 64-bit
  state and a 32-bit `std::size_t` holds its low half; `tests/unit/test_tuple_hash.cpp` pins a few keys' hashes.
- **A `thread_local` at namespace scope is `constinit`.** Under the MSVC ABI (cl.exe and clang-cl) a dynamic
  initializer of one runs from a TLS callback at the start of every thread - the loader's worker threads too, before
  the CRT has set up stdio - where a CRT call can end the process before `main`, now and then (the logger's
  `_isatty(_fileno(stderr))` did, in about one run in twenty). GCC and Clang elsewhere initialize it on first use, so
  only Windows shows it. Hold a constant (`nullptr` for stderr, an unknown state) and compute on first use, as
  `logger.hpp` and `internal/backend/row_index.hpp` do; a `thread_local` inside a function is initialized on
  first use everywhere.
- **The MSVC CRT's deprecation warnings count** (`fopen`, `getenv`, ... under `-Wdeprecated-declarations`): a header
  that warns there warns in a user's Windows build. Call the CRT's own spelling in the helper's `_WIN32` branch
  (`_dupenv_s` for `getenv`; `_fsopen` for `fopen`, since `fopen_s` opens the file unshared); never define
  `_CRT_SECURE_NO_WARNINGS` in a header, where it would reach the user's code.
- **Checking.** The standard library's common ground is the libcxx job's (libc++ 18 and the newest) beside the
  GCC and Clang jobs' libstdc++; the MSVC STL is checked with Windows, below. WebAssembly is CI's wasm job: the emsdk
  image at one pinned version (`docker.io/emscripten/emsdk:6.0.13`, in `ci.yml` and `ci/local.py`), for
  Emscripten whose LLVM and libc++ are 18 or later; the HiGHS backend there is the wasm-highs job's, with HiGHS
  built by `ci/containers/build-highs-wasm.sh`. HiGHS's simplex and MIP need a stack of 8 MB there
  (`-sSTACK_SIZE=8MB`, which the wasm presets give the tests): Emscripten's default of 64 KB overflows, unchecked, into
  the memory around it.
  Locally: `podman run --rm -v "$PWD":/src -w /src docker.io/emscripten/emsdk:6.0.13 sh -c 'cmake
  --workflow --preset wasm'`. Windows is CI's windows job: everything in stub mode and the MathOpt and HiGHS backends
  on the OR-Tools prebuilt for Visual Studio 2022, built through CMake by cl.exe and by clang-cl at `/W4 /WX`, `/O1`
  with the debug checks on, with Visual Studio 2026 on the `windows-2025` runner (the `windows-cl` and
  `windows-clang-cl` presets) and 2022 on `windows-2022` (`windows-vs2022-cl`, `windows-vs2022-clang-cl`); the
  compile_fail cases and syntax checks run the compiler with MSVC's options (`tests/cases.py`), in Visual Studio's
  environment. macOS is CI's macos job: everything in stub mode, the HiGHS backend on HiGHS from Homebrew and the
  MathOpt backend on the OR-Tools prebuilt for arm64 macOS, with Apple Clang on Apple's libc++ (the `macos` preset).
  Both then install the CMake package and build a project on it. Neither runs in `ci/local.py`; a pull request runs
  them, and a work branch by hand (`ci.md`).

## Compilers

- **Compiler support** is a range tested at both ends, nothing in between: the floor is GCC 13 / Clang 18, with
  LLVM's libc++ 18 beside libstdc++ (raising it is a decision, not a cleanup); the ceiling is the newest releases,
  bumped at each major (`ci.md`, "Bumping"). A new warning or a changed first error there is what users of that
  compiler will see: fix it, then bump. Emscripten is not a range: one version is pinned, and moving it is a tool
  update.

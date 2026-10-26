<!-- Please write in English. For anything larger than a fix, agree on the design in an issue first. -->

## What and why

<!-- What this changes, and why. Link the issue it closes: "Closes #123". -->

## Checklist

- [ ] Every commit is signed off (`git commit -s`, Developer Certificate of Origin).
- [ ] `tests/run_tests.sh` passes with GCC and with Clang (`CXX=clang++-18`); `--mathopt` too if the MathOpt backend or the shared machinery is touched.
- [ ] `tools/format.sh --check` passes and `tools/tidy.sh` reports nothing.
- [ ] A new behavior has a test; a new compile-time message has a `tests/compile_fail/` case whose `// expect:` line it prints first.
- [ ] Documentation that mirrors code is updated: the guide and README code with `tests/test_readme.cpp`, the API reference (English and Korean), the skill in `skills/dreamor/`.
- [ ] A claim of speed comes with a measurement (`bench/catalog/run.py --builds`, see `bench/README.md`).

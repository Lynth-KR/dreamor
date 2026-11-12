<!-- Please write in English. For anything larger than a fix, agree on the design in an issue first. -->

## What and why

<!-- What this changes, and why. Link the issue it closes: "Closes #123". -->

## Checklist

- [ ] Every commit is signed off (`git commit -s`, Developer Certificate of Origin).
- [ ] What AGENTS.md's "Done means" lists passes: the tests on GCC and Clang, a backend's if it is touched, `tools/format.sh --check`, `tools/tidy.sh`.
- [ ] A new behavior has a test; a new compile-time message has a `tests/compile_fail/` case whose `// expect:` line it prints first.
- [ ] Documentation that mirrors code is updated: the guide's pages with their tests under `tests/docs/`, the README's output block with its program, the API reference, the skill in `skills/dreamor/`.
- [ ] A claim of speed comes with a measurement (`bench/catalog/run.py --builds`, see `docs/bench/index.md`).

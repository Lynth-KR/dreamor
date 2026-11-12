# Contributing to dreamor

Thank you for your interest. dreamor is maintained by one person ([@Lynth-KR](https://github.com/Lynth-KR)), and bug
reports, questions, fixes and ideas are all welcome. Please write issues, pull requests and discussions in English.

## Asking, reporting, proposing

- **A question** - how to model something, why a call does not compile - goes to
  [Discussions](https://github.com/Lynth-KR/dreamor/discussions).
- **A bug** goes to an issue, through the bug report form. The smallest code that shows it helps most: a set, a table
  and a few rows usually do.
- **A feature** goes to an issue, through the feature request form. Start from the modeling problem - the formula, in
  math or in words - and the code you write for it today: a feature is judged by the models it makes clearer.

dreamor is small on purpose, and much of what makes it useful is what it leaves out. Please read the
[philosophy](docs/philosophy.md) before proposing a change; every change is weighed against it. For anything larger
than a fix, open an issue first and agree on the design there, so that your work is not spent on a pull request that
cannot be merged.

## Working on the code

[AGENTS.md](AGENTS.md) is the map, for people as much as for coding agents: the layout, the commands to build and
test, and a table of which rule to read before which change. The rules themselves are in [docs/dev/](docs/dev/), a
file a topic. To start:

```sh
cmake --workflow --preset stub-gcc13-release   # configure, build and test one CI job, as CI runs it
cmake --list-presets                           # the others
```

The tests need CMake 3.25, Python 3.12 and GCC 13 or Clang 18 or newer; the formatter and clang-tidy run through
[uv](https://docs.astral.sh/uv/), which is all you install for them. A change is done when it passes what AGENTS.md's
"Done means" lists.

## Commits and pull requests

- **One topic a commit**, with a message that says what and why.
- **Sign off every commit** (`git commit -s`). The `Signed-off-by:` line certifies the
  [Developer Certificate of Origin](https://developercertificate.org/): that you wrote the change or have the right to
  submit it under the project's license, MPL 2.0. A pull request with commits not signed off cannot be merged.
- **CI** runs on every pull request ([the jobs](docs/dev/ci.md)). CPLEX is not on CI: if you touch code facing Concert
  and have CPLEX, run the `cplex` preset.
- The maintainer may squash or reword commits so that the history reads by topic.

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).

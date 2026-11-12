# Release notes

For people working on dreamor; `AGENTS.md` says when to read it.

What changed is told in the GitHub release notes alone; there is no `CHANGELOG.md`. The next version's notes collect
in its draft release. Their readers are the library's users, and they read a release's notes to decide whether to
upgrade and what upgrading takes, so the notes say that and no more, in three parts:

- **What's new**: what a user gains, a line each - new features, faster parts, and the fixes worth upgrading for.
- **Upgrading from x.y**: what code written for the version before has to change, shown in code; or one line saying
  nothing does.
- **Requirements**: compilers, solvers and libraries, only when they change.

No figures, no design reasoning, no account of how a bug came about, and no CI, benchmark, test or tooling change:
the commit messages hold all of that.

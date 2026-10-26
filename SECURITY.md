# Security policy

## Supported versions

dreamor is before 1.0: fixes go into the latest release only.

## Reporting a vulnerability

Report it privately, through GitHub's [private vulnerability reporting](https://github.com/Lynth-KR/dreamor/security/advisories/new)
(the repository's Security tab, "Report a vulnerability") - not in a public issue or discussion. Say what goes wrong,
with the smallest code that shows it, and the dreamor version, compiler and backend.

dreamor is maintained by one person: expect a first answer within a week. A confirmed report is fixed in a release,
and the advisory is published with it, crediting you unless you ask otherwise.

## What counts

dreamor is a header-only library that runs inside the program that uses it; it reads no input of its own, opens no
file and no connection. A vulnerability is code in dreamor that corrupts memory or behaves undefined while the
program keeps every documented precondition.

Breaking a precondition is not one: a source handed to `makeSet` that holds an element twice is undefined behavior
by contract, as with the standard library's preconditions, and a debug build (no `NDEBUG`) stops on it with a
message. If you find a precondition that a debug build does not check and that is easy to break unknowingly, a
regular issue is welcome.

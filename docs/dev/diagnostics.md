# Diagnostics

For people working on dreamor; `AGENTS.md` says when to read it.

Misuse is stopped at compile time with a `static_assert` whose message is one sentence saying what to write
instead (e.g. `sumExpr(...) is not an expression yet: add .build() - ...`). When adding or changing a diagnostic:

1. Add a case to `tests/compile_fail/` (Concert/set layer), `tests/mathopt/compile_fail/` or
   `tests/highs/compile_fail/`, in the topic file it belongs to (`columns.cpp`, `chain.cpp`, `vartable.cpp`, ...;
   a new topic is a new file that starts with the license header and includes `prelude.hpp`). A case is a block
   from `#ifdef DREAMOR_CASE_<name>` to `#endif  // DREAMOR_CASE_<name>`, its `// expect: <text>` the line right
   after the `#ifdef`; the runner compiles each case alone, with `-DDREAMOR_CASE_<name>`, and no two cases of a
   folder share a name.
2. The runner requires `<text>` in the compiler's **first** error and its notes, not just a rejection. This must
   hold on every compiler CI runs - GCC 13 and Clang 18, the newest releases, Emscripten's em++, Apple Clang, and
   cl.exe and clang-cl of Visual Studio 2022 and 2026 - whose jobs check the cases too. cl.exe instantiates more
   than the others before a `static_assert` (`platforms.md`), so a check that only it trips shows there first. Arrange overloads so the dreamor message comes first, not a wall of template
   errors.
3. `control.cc` in each folder must keep compiling; it proves that a rejection comes from the library.
4. Say when the message comes and what to write instead, on one of the cases that expect it - the same message
   pinned by several cases is explained once - in two lines after its `// expect:`, a line that is `//` alone between
   them, so that the formatter's wrapping keeps them apart:

   ```cpp
   #ifdef DREAMOR_CASE_lateral_reads_a_later_column
   // expect: lateral and where read only the columns before them in the chain
   // when: A lateral source or a chain's `where` reads a column no earlier step brings.
   //
   // fix: Move the step after the source of that column, or drop the parameter.
   int main() {
   ```

   A case that C++ itself rejects, with no `// expect:`, says the same right after its `#ifdef`. A case's own
   comments come after its first line of code, or after a blank line: the fix runs on over every comment line after
   it. `tools/gen_reference.py` writes the Compile-time messages page from these lines, a section a topic file
   (`tools/refgen/messages.py` lists them), and fails a message that no case explains, one that two cases explain, a
   case with no message and no explanation, and a topic file in no section.

A broken contract - a precondition such as "no source holds an element twice" - is undefined behavior, and only a
debug build checks it: `DREAMOR_DEBUG_FATAL` prints one sentence naming the contract and aborts, as libstdc++'s
assertions do. Each check has a case in `tests/contract_fail/` (`tests/highs/contract_fail/` for the HiGHS
backend's own), a block of a topic file as above: a program that breaks the contract, with the same
`// expect: <text>` line, which the runner looks for in what the program prints before it stops, and on the line
after it `// contract: <name>`, the `@contract` it checks (`comments.md`). `tools/gen_reference.py` fails a case that
names no `@contract`, and a `@contract` that no case names.

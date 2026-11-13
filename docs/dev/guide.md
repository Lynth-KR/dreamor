# The guide

For people working on dreamor; `AGENTS.md` says when to read it.

## What the guide is

The guide (`docs/guide/`) is a how-to: each section takes the reader through one thing a model does, in code they can
copy, and says what that code makes. Its reader wants to get that thing done, knows C++ and the model's
mathematics, has not used dreamor before, lands on a section by a search or a link rather than by reading from the
top, and may read English as a second language.

The API reference (`docs/reference/`, generated from the headers' `///` comments) is the other kind of document: its
reader knows what they are looking for and checks one rule. The reference holds the rules - what a call takes, what
does not compile, what a debug build checks, what it costs - each once; the guide links to them. So the measures
differ. In the reference, repetition and explanation are noise. In the guide, a fact said in the code and again in
the prose is often what makes the section clear; this document says when it is.

## Pages and sections

- **A page a topic**, never one long file, in the reading order of `zensical.toml`'s nav - a new page goes there, or
  the sidebar does not show it; the reference's part of the nav is `tools/gen_reference.py`'s. Building a set from
  nothing (relational algebra, set-builder notation, records) and deriving one from a set (`where`, `select`,
  `orderBy`, membership; set algebra) are pages of their own.
- **A page opens** with what its topic is, in plain words, and the model's names that its sections share, each with
  a link to the page that defines it. A name that one section alone uses is introduced in that section. A region
  that most sections use, such as the set they derive from, may open the page too, with its steps and its result.
- **A section a task**, its heading phrased as what you do ("Match rows with a set"), its parts in the order of "A
  section" below.
- **A page ends** with "See also": the reference page of its topic, the guide pages beside it, and the debug-build
  checks its code can meet.
- **The solver side is HiGHS.** Concert and MathOpt come in only where dreamor's own spelling differs (`VarTable`,
  `sumExpr`, what `solution()` takes): a sentence and a link to their reference pages. The solvers' own syntax for
  rows and objectives is theirs to teach, not the guide's.

## A section

A section is one task, in five parts, in this order:

1. **The goal**, before the code: one or two sentences that say what you want and why a model needs it, in the
   model's terms ("every combination of a machine, an item that the machine makes, and a period"), not the API's.
   A task the model does not have is stated as a supposition ("Suppose each machine is serviced in periods 1 and
   3"), and a type the page declares for it is called the page's own ("`Fixture` is this page's own type, not part
   of the model").
2. **The code**: a region of the page's test, with its comments, or two regions where the first declares what the
   second uses (a type, then the code that fills it). A section on what differs on another backend may have no
   code: its goal, a list of the differences, and the link.
3. **The steps**: what each call adds, keeps or returns. A region of several calls or statements gets a bulleted
   list, an item a call; a region of one or two calls gets a sentence.
4. **The result**: what came out, in words and in the model's terms, and why - the data that makes it so. A count
   comes after the description it sums up, never in its place, and only where a reader would work it out to check
   their understanding (2 machines in 3 periods: 6 rows), not where the description already names every row.
5. **The link**, a sentence that says what the reader finds there: "For what each step takes, see [`from`](...),
   [`lateral`](...) and [`where`](...)." Not a list of links in parentheses after another sentence.

A concept that the page has not met yet is explained in plain words where it first appears, even if the reference or
another page explains it too. A section that uses a name an earlier section introduced recalls it in a few words:
"`sSOLD`, from the section above, holds the items that customers buy."

### What the parts may repeat

The code and the prose each stand on their own. A reader who reads only the code, as many do and as the skill's
copies invite, sees what each line makes from its comments. A reader who reads only the prose knows what to do and
what comes out. A fact that both need is in both; a fact that neither needs is in neither. Each part has its own
job, and the job says what it may repeat:

| Part | Its job | May repeat |
|---|---|---|
| A comment in the region | The value a line makes (`// gear, washer`), what a name or a constant stands for (`// a batch of 8 units`), or why a line is written as it is (`// std::max deduces one type`), in a few words. Optional. | A value the result states. |
| The steps | What each call adds, keeps or returns: the mechanism. | Nothing: values are the result's. |
| The result | What came out and why. | A comment's value, when it gives the reason. |
| The link | Where the rules are. | Nothing. |

Three rules decide the cases the table leaves open:

- **The result repeats a comment's value only with its reason.** Under `// 12 rows`, "`sBATCH` holds 12 rows." says
  nothing the code did not; "M1 has 4 hours in a period, so 8 frames, which take 8 hours, do not fit on it. ... each
  in the three periods: 12 rows." does.
- **A call is explained the first time the page uses it.** "`at` returns a `Ref` to the item with the key" belongs
  to the section that first looks an entity up; a later section that calls `at` to get M1 names the call without
  explaining it again, unless it uses the call in a new way.
- **The guide states what its code exercises, and links the rest.** A behavior that the region shows and a check
  holds may be said, even if the reference says it too: the look-up region calls `at` with a key it holds and its
  comment names the exception for a key it does not, so "If no item has the key, `at` throws `std::out_of_range`."
  is the section's. What the region does not exercise - another requirement, a compile error, a contract, a cost -
  is the reference's, and the section links to it. A section whose task is a cost ("Build the selective part
  first") says what its code does differently, held by a check such as the visit order, and leaves the numbers to
  the reference.

## The code

- **It lives once, in a test**, and the page pulls it in. A page's code is `tests/docs/guide/<page>.cpp` (no solver)
  or `tests/docs/highs/<page>.cpp` (HiGHS); the code a reader sees sits between `// --8<-- [start:name]` and
  `// --8<-- [end:name]`, and the page holds `--8<-- "tests/docs/guide/<page>.cpp:name"` in a fenced block
  (Zensical's `pymdownx.snippets`, `zensical.toml`). A region holds code a reader would write; the checks
  (`DREAMOR_CHECK`, `static_assert`) stay outside the regions.
- **Every claim about what the code makes or does is held by a check**, or the sentence goes. A check reads the
  value the region made; it never copies the region's expression to compute it again. A count is one a reader can
  work out from the model's data, and the check's comment says how. A fact of the model's data ("M1 has 4 hours in
  a period") is held by the model's code, which the Entities page shows; a definition of a concept needs no check.
- **A comment in a region** is a comment of example code, not of the library: `comments.md`'s rule that a `//` is
  a complete sentence that repeats nothing does not apply to it. It is a few words that show a value or name what a
  line stands for, it is true, and a check holds the value it shows.
- **One model**, `tests/docs/guide/model.hpp`: a multi-level lot sizing - its entities, its bill of materials, its
  columns, its sets written as chains and in set-builder notation - and, on HiGHS, `tests/docs/highs/plan.hpp`: its
  variable families, rows and cost. A part goes into either only when two pages use it and it belongs to that world;
  anything else a page declares in its own test. `tests/docs/highs/model.cpp` builds the model whole, checks that the
  two notations give the same sets, and solves each to the cost of its plan worked out by hand.
- **The model code's rules** are `code-style.md`'s, "Model code in this repository": names, named callables, sets
  from relations, text and messages (no `std::cout`), plain C++ types. The tests are the code users read:
  they build with `user`'s flags and `tests/docs/.clang-tidy`.

## Sentences

The guide follows the [Google developer documentation style guide](https://developers.google.com/style), as the
reference does.

- **Address the reader as "you"**, as Google's guide does: "You can build the same set in set-builder notation."
  Never "we" or "let's", and never "a model's author" or "the user" for the reader.
- **Tell the reader what to do in the imperative:** "Start the chain with `from`." A sentence that says what code
  does is in the present tense, with the code as its subject: "`where` keeps the rows for which every condition
  holds."
- **Put the goal or the condition first:** "To keep only the rows that fit, add `where`.", not "Add `where` to keep
  only the rows that fit."
- **`comments.md`'s "Sentences" and "Terms"** (under "Writing a doc comment") hold for the prose: one fact a
  sentence, conditions first, a subject that does the verb, dashes sparingly, comparisons after the behavior, no
  promise the code does not keep, nouns over unclear pronouns, no facts in parentheses, one term for one thing. Two
  of them are the reference's alone: "No usage" (usage is the guide's subject) and "The signature's names". In a
  bulleted step, two sentences about one call are one item.
- **Mathematics is written in set-builder notation**, in backticks: a set as
  `{(m, i, t) | m ∈ machines, i ∈ items(m), demand(i, t) > 0}`, a sum as
  `Σ_{(m, i, t) ∈ produce} time(i) * make(m, i, t)` or `Σ_{i | (m, i, t) ∈ produce} ...`, a row as
  `... <= capacity(m)`. Not in words ("sum over ... of ...").

## An example

[Relational algebra](../guide/relational-algebra.md) is the page to read before writing one: every section keeps
this document, and it reads as a story from the first chain to the visit order. Its first section, before:

> `sBATCH` holds each machine, each item it makes and each period, where a batch of 8 units fits the machine's hours:
> M1's plates and brackets and M2's bolts and nuts, 12 rows. `lateral` gives each machine its own items, `cross`
> pairs every row with every period, and `where` keeps the rows `fits_batch` holds for.

The goal comes after the code, the steps and the result are run together in clauses, and the count stands in the
middle of the description. After:

> Suppose you want every combination of a machine, an item that the machine makes, and a period, but only where a
> batch of 8 units of the item fits in the machine's hours for one period. Start a chain with `from`, and add one
> source or condition at each step:
>
> *(the code; its comment `// a batch of 8 units` names the constant)*
>
> - `from<cMachine>(machines)` starts the chain with every machine.
> - `lateral<cItem>(items_of)` adds the column `cItem`, with the items that each machine makes.
> - `cross<cPeriod>(dreamor::interval::closed(1, periods))` pairs every row with every period, 1 to 3.
> - `where(fits_batch)` keeps only the rows where 8 units of the item take no more than the machine's hours.
>
> M1 has 4 hours in a period, so 8 frames, which take 8 hours, do not fit on it. M2 has 3 hours, so 8 plates, which
> take 4 hours, do not fit on it. `sBATCH` holds M1 with the plate and the bracket, and M2 with the bolt and the nut,
> each in the three periods: 12 rows.
>
> For what each step takes, see [`from`](...), [`lateral`](...), [`cross`](...) and [`where`](...).

The steps say what each call does and give no value; the result gives the rows and the data that decides them, and
the count last.

## Checking a page

Read the page as its reader would, from a section's heading to its link, and check:

- **Two readings.** The code with its comments alone shows what each line makes; the prose alone says what to do and
  what comes out. If either reading leaves a gap, a part is missing; a repetition that neither reading needs goes.
- Each section has its five parts in order, and its goal is in the model's terms.
- Every sentence speaks to "you" or about the code, and keeps the sentence rules above.
- Every claim about what the code makes or does is held by a check in the page's test.
- Nothing the region does not exercise - a requirement, a compile error, a contract, a cost - is stated; it is
  linked.

`tools/sync_skill_docs.py` copies every page into `skills/dreamor/docs/`, its code written out, for the skill, which
is copied into a project alone; `--check` fails on a copy that is not the page's. `tools/site.sh` builds the site with
every warning an error: a region its test no longer has, a link to a page or an anchor that is not there. CI's lint
and `ci/local.py`'s first phase run it. The tests run with the rest: the stub presets run `tests/docs/guide/`, a
HiGHS preset `tests/docs/highs/`. A sentence that has gone stale no tool finds: read the page again when its code, or
the API it shows, changes.

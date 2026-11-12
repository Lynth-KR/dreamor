# Comments

How the code's comments are written, for people working on dreamor. A `//` comment is for whoever works on the code;
a `///` comment is a user's, the source of the API reference. `AGENTS.md` points here, and nothing else says it. Both
are formatted by `tools/format.sh` like the code around them, and read as it leaves them.

## Comments for the code - `//`

A `//` comment says what the code cannot: why it is written so where the obvious way is not taken, what it assumes or
keeps (an invariant), a workaround and its cause (a compiler's bug number, a standard's clause). A line or two,
beside the code it speaks of, with no blank line between them. What the code already says is not repeated, nor what a `///` block or another `//`
comment beside it says; code that needs a paragraph to follow is rewritten instead.

Its style follows the part of the code it is in, because each part's comments do another job:

- **The library's implementation** (`include/`), **the tools and the CI** (`tools/`, `ci/`): the reasons and the risks
  the code cannot state - why the obvious way is not taken, what an invariant keeps, a workaround and its cause. Its
  reader knows the code around it but is not its author, so it is written by the sentence rules of "Writing a doc
  comment" below: complete sentences in plain words, conditions with "if", one term for one thing, no idioms, no
  compressed clauses.
- **The tests** (`tests/`) and **the benchmarks** (`bench/`): the contract a case checks and what it expects, or what
  makes a measurement stand for dreamor's performance without distorting it. Compressed technical prose is right
  there: a case's heading may be a noun phrase ("empty set", "defaults, unnamed"), and a line may drop the words a
  full sentence would need, as long as what is checked or measured is unambiguous.
- **The guide's code** (`tests/docs/guide/`, `tests/docs/highs/`) is read on the guide's pages, by users: `guide.md`
  says how its comments are written.

- **A file** starts with the license lines (`code-style.md`, "Library code"), then one line saying what it holds.
- **Not in a header**, and where it goes instead:
  - how a part is used: the guide, whose code the tests compile;
  - what a public part is and does: its `///` block, or its reference section's introduction (`@section`);
  - a design's reasoning, the alternatives set aside, precedents: the commit message, or `docs/philosophy.md` for a
    principle a user meets - at the code, only the line that keeps the next reader from "fixing" it;
  - history ("used to", "now"), measurements, how a bug came about: the commit message.
- **No code block** but the code itself written out (what a macro expands to, how a third-party include is wrapped),
  never a call.
- **A change keeps its comments true**: one the code no longer bears out is fixed or removed in the same change. No
  commented-out code, no banner lines.
- **NOLINT** takes its reason as `code-style.md`, "Tools", says.

## Doc comments - `///`

The API reference (`docs/reference/`, a page a section and `index.md`) is generated from the headers' `///` comments by `tools/gen_reference.py`,
nothing in it written by hand: after a change to a block, `python3 tools/gen_reference.py --out docs/reference`, and
CI's lint checks that the two agree. A hole the format leaves is fixed here when it is found.

- **What carries one.** Every declaration a user names, in `dreamor` or a component's namespace and not in
  `internal` or `detail`: a function, a class or struct, a public member of one, a concept, a constant, a type
  alias, and a macro named `DREAMOR_...` but not `DREAMOR_INTERNAL_...`, at its first definition. One without a
  `///` block, and without `@hidden`, fails the tool. What declares nothing new carries none: a forward
  declaration, a specialization, a deduction guide, a member defined outside its class, a destructor, a function
  `= default` or `= delete`, an inheriting constructor, a friend declaration without a body, a member type with a
  name the standard gives one (`value_type`, `iterator`, `key_type`, ...).
- **Where.** Right above the declaration, with nothing between them but a `NOLINTNEXTLINE` line, which must be
  right above the line it excuses; the tool fails any other `//` line there. A `//` note on the declaration goes
  into its body (the first lines of a function's or a class's body); a note on a group of declarations, or on one
  with no body, stands above the `///` block as a paragraph of its own, with a blank line after it. A member is
  documented at its declaration in the class, never at a definition outside it.
- **Shape.** A summary sentence (see "Writing a doc comment" below), then paragraphs of Markdown, then the fields
  that say something, in this order:

      /// Returns the rows of `A` that are not in `B`, in the order of `A`.
      ///
      /// Takes: what the signature does not say of the arguments - a set of two columns over one value type.
      /// Returns: what the signature and the summary do not say of the result.
      /// Throws: `std::out_of_range` if the key is not in the set.
      /// Contract: no_repeat_in_source
      /// Cost: `O(|A| + |B|)`, one index over B.

  `Throws:` is a clause an exception, each starting with its type in backticks, separated by `;` - the
  "Exceptions" section is gathered from them. A field starts a `///` line of its own, and a field's name later in a
  line fails the tool: a reflow can pull a field into the paragraph above it. A field runs on to the next field, marker or blank `///` line, whatever its lines' indentation - clang-format
  breaks a long line with none. Code - a name, a type, an expression, `O(...)` - is in backticks:
  `<` and `|` outside them break the Markdown. No usage and no code block: that is the guide's, whose code the tests
  compile. English, ASCII.
- **An entry is known by its scope and short name** - its public namespace, and its class for a member. A block with
  no marker is a main entry, its own box. Two main entries may share a name when they mean different things
  (`select<A, B>()` and `select(expr)`).
- **Markers**, each a `///` line of its own:
  - `@overload` - another form of a main entry of the same scope and name: the nearest one above it in the same
    header, else the only one in any header; none, or several elsewhere, fails the tool. An `@overload` with no
    text is the same entry in another form (the `&&` and `const&` forms of one step): its signature joins the box of
    the nearest entry above it of the same scope and name in the same header, the main entry or an `@overload` with
    text. Forms that take their operands in another order (`c * x` and `x * c`) are different overloads, each
    with text of its own. An `@overload` with text differs in its signature or its requirements, and is an entry of its own:
    it gets a box of its own under the main entry's heading, with its signature, a summary sentence, a body and
    fields, all its own; an `@overload` with fields and no summary fails the tool.
  - `@hidden - reason` - a declaration in a public namespace a user never writes: an overload that exists only to
    reject a call with a message, a trait the API needs at namespace scope, or the same overload for a container
    passed as an rvalue, which differs only by Clang's lifetime mark (`DREAMOR_LIFETIMEBOUND`) and is documented by
    the entry it repeats. No entry; the reason is required.
  - `@section Title` - in a block of its own: the section the entries after it in this header belong to, until the
    next `@section`; an entry before any fails the tool. One header may open several sections, and several headers
    one section. `tools/reference_sections.txt` holds the page's order, a line a section - `Title | header, header`,
    a header named under `include/dreamor/`, a `#` line a comment - the headers in the order their entries come, and must match the `@section`s the headers open, both ways. The
    paragraphs after `@section` introduce the section, and `Guide: guide/<page>.md` links the guide page that shows it in use; at most one
    header gives a section its introduction. Every section has an introduction, and every section but the two
    gathered ones a `Guide:` line; one without fails the tool. No subsections.
  - `@contract name: text` - a contract, defined once, in the header whose code checks it; an entry's `Contract:`
    field names it. The text's first sentence states the contract whole; the tool writes it into the entry with a
    link to the Debug-build checks page, which shows the whole text. A `tests/contract_fail/` (or `tests/highs/contract_fail/`)
    case checks it in a debug build by naming it on the line after its `// expect:` - `// contract: <name>` - and its
    `// expect:` line is the sentence the check stops on. Every case names one; a contract may have several, and
    one with none is listed as unchecked.
  - `@anchor id` - the id of the entry's heading, which a link names (`indexset.md#indexset-select-columns`). Without
    it, the id is the entry's short name in lower case, an operator's spells its symbol (`operator-star`), a member's
    follows its class's short name (`ref-operator-arrow`), and a constructor's is `<class>-constructor`. Two headings
    on one page with one id fail the tool until each names its own, so that a new entry never takes the links of an
    old one.
  - `@exposed` - on an internal class whose members a model calls without naming the class: what a public function
    returns (`internal::Chain`, which `index_set::relational::from` gives) or what every backend's `VarTable` is
    built on (`internal::VarTableCore`). It is documented under its own name, the one a compiler's diagnostic shows,
    and its public members each need a `///` block.
- **What the tool makes.** For each section, in order: `## Title`, its introduction, then for each entry a heading
  with its short name and an admonition box, `!!! abstract "<qualified name> - <kind>"`, holding the signatures
  (clang-format at 88 columns, without `[[nodiscard]]` and the specifier macros such as `DREAMOR_LIFETIMEBOUND`), the summary, the body and the fields; then a box for each `@overload` with text of
  its own. A class's members follow it, a heading level down. The kind is read off the declaration: function, member function, constructor, class, enum, concept,
  constant, variable, type alias, macro. Two sections are gathered, not written: "Exceptions", every `Throws` by
  exception type with the entries that throw it (a fact across many entries - more than 2^32 - 2 rows to index -
  goes in that section's introduction, once), and "Debug-build checks", every `@contract` with the entries that
  name it, the sentence its check stops on and its case. The tool also fails a function that returns a value
  without saying so: its summary starts with `Returns`, or it has a `Returns:` field.

## Writing a doc comment

The format above says where a `///` block goes and what the tool makes of it; this part says how its sentences are
written. A `///` comment is written for a user who will look the part up one day: in the polite, standard style of
reference documentation. A reader of the reference is a C++ programmer who has not read dreamor's code, and may read
English as a second language.

### The base: Google's developer documentation style guide

`///` comments follow two pages of the
[Google developer documentation style guide](https://developers.google.com/style), except where this document says
otherwise:

- [API reference code comments](https://developers.google.com/style/api-reference-comments): what the first sentence
  of a class or a method says, present tense, how return values and exceptions are worded.
- [Write for a global audience](https://developers.google.com/style/translation): short sentences; subject, verb,
  object; conditions first; no idioms, colloquialisms or humor; no dropped `that`, `which` or `then`; one term for one
  concept; a noun in place of a pronoun whose antecedent is unclear.

In short: complete sentences, each saying one thing, in plain words. Where the guide and this document differ, this
document holds.

### The summary sentence

The first sentence says what the entry is or does. It does not repeat the entry's name and does not start with "This
class" or "This function".

| Entry | The summary starts with | Example |
|---|---|---|
| A function or member function that does something | a verb, third person singular present | Inserts `entity` unless an element with the same primary key is already held. |
| A function that only returns a value, not a `bool` | `Returns` | Returns the first element. |
| A function that only returns a `bool` | `Checks whether` | Checks whether the vector holds no elements. |
| A constructor | `Constructs` | Constructs an empty vector that is being filled. |
| A class, struct, enum, concept, type alias, constant or variable | a noun phrase | A reference to an entity of a frozen `EntityVector`. |
| A macro | a verb, third person singular present | Declares an entity column `NAME` over the entity type `T`. |

Google's guide starts a getter with "Gets"; dreamor writes "Returns", as cppreference does for C++. A function that
returns a new object built from its arguments and `*this`, as a chain's step does, only returns a value.

### The other parts of a block

- **An `@overload` with text** is an entry of its own, in a box of its own: it has a summary sentence by the table
  above, a body and fields, and says everything a reader needs who lands on it alone - by a search, or a jump to its
  symbol. What the main entry or the section introduction says is said again where this form needs it; nothing is
  left out because it is written elsewhere. Its first sentence names what is different about the form ("Returns
  the first row of the group that `key` identifies."); a form that exists only under a condition may start with the
  condition ("If the primary key is a `std::string`, ...").
- **A section introduction** (`@section`) starts with what the section's parts are for, and ends with its `Guide:`
  line. Another section is named as "the Set algebra section".
- **A `@contract` text** starts with one sentence that states, in the present tense, what holds when the contract
  is kept: "An `EntityVector` is frozen before it is iterated." Every entry that names the contract shows this
  sentence alone, with a link; the sentences after it - the cases it covers, what it includes - appear on the
  Debug-build checks page only.

### The fields

A field is one paragraph: the tool joins its lines into one. A list that a field would need goes in the body, and
the field says what the list does not.

- **`Returns:`** A function that returns a value says what it returns, in one of two ways:
  - a function that only returns a value states it in its summary, which starts with `Returns`; it adds a
    `Returns:` field only for what the summary does not say (an order, an empty result);
  - any other function - one that does something and returns a value, or a `bool` query - has a `Returns:` field.

  A non-`bool` value is described starting with "The" or "A", and may add the cases that change it ("An empty view
  if `first` is greater than `last`."); a `bool` as "True if ...; false otherwise." The field of a simple `bool`
  query repeats its summary ("Checks whether the vector holds no elements." and "True if the vector holds no
  elements; false otherwise."). That is accepted: a reader of the reference reads the summary and the fields apart,
  and a rule to tell the cases apart costs more than the repetition. A `//` comment never repeats (above).
- **`Throws:`** The exception type in backticks, then the condition starting with "if": "`std::out_of_range` if no
  element has the key."
- **`Takes:`** Everything about the template parameters, the arguments and the object itself (`*this`) beyond what
  their types say, in one place: what they must be ("`T` must satisfy `entity`."), what they may be ("The entity
  type may be incomplete."), what an argument left out means, and the limits on a value. A requirement that, broken,
  does not compile is stated as a requirement; the error messages themselves are listed in
  `skills/dreamor/references/diagnostics.md`.
- **A precondition that no debug build checks** is a sentence of its own that says what happens if it is broken, as
  cppreference does: "If the vector is empty, the behavior is undefined." A precondition that a debug build checks is
  a contract, named in the `Contract:` field.

### Sentences

- **One fact a sentence.** Facts that are not about the same thing go in separate sentences. Behavior that depends on
  a case is a bulleted list, one case an item, not clauses joined by commas, dashes and semicolons.
- **Conditions with "if" or "when"**, before what they govern: "If the column is already bound, ...", never "the
  column bound already, ...".
- **A subject that does the verb.** What fails to compile is a program or a call, not an object: "A step that adds a
  column the chain already has does not compile", not "the chain stops".
- **Dashes sparingly.** At most one dash pair (` - ... - `) in a sentence; an aside long enough to need more is a
  sentence of its own.
- **Comparisons follow, never replace.** A comparison with the standard library ("Like `std::map::at`, ...") may
  follow the sentence that states the behavior in its own words. A comparison alone ("`std::map::find`'s answer"),
  and a figure of speech ("a table refusing an `INSERT`"), say nothing to a reader who does not know the thing
  compared.
- **No promise the code does not keep.** Every statement is checked against the code it documents; what the code
  does not guarantee is not written as if it did.
- **Nouns, not bare pronouns.** "it", "one" and "they" stand where the noun they replace is in the same sentence or
  is the subject of the sentence before: "A `Ref` does not own its entity, and it gives only `const` access to the
  entity." Elsewhere the noun is written: "A default-constructed vector is empty", not "A default-constructed one is
  empty". The rule is for clarity; repeating a noun where a pronoun is clear makes a sentence worse, not better.
- **No facts in parentheses.** A fact worth saying is a sentence or a list item; parentheses hold only an
  abbreviation or a name for what was just said ("the product (`cross`)").
- **The signature's names.** A parameter is called by its name in the signature: `Column`, not `C`, if the
  signature says `Column`.
- **No usage; mathematics is welcome.** No call written out, inline or in a block - how a part is used is the
  guide's. A sentence may point to another part for another need: "For a list that depends on the row, use
  `lateral`." The mathematics that defines what a part does is specification, not usage, and is written freely: a
  model, an objective, a constraint (`lower <= expression <= upper`, `c'x + 1/2 x'Qx`, "every pair `(a, b)` such
  that ...").
- **Lists** are bulleted, one level deep, each item a sentence or a phrase that ends in a period; an item's
  continuation lines are indented two spaces.

### Terms

dreamor's concepts have one name each, used in that sense alone. The table is for the writers, to keep one word
for one thing; a reader of the reference never sees it, so the text itself must be clear without it. Most of these
are the standard words of relational data and of compilers (row, column, key, bind, lower, fill); where one of them
would leave a C++ programmer unsure in its context, the sentence says it more plainly there.

| Term | Meaning |
|---|---|
| entity | An object of the user's type held in an `EntityVector`, identified by its primary key. |
| element | What a container holds. An `EntityVector`'s elements are entities; "element" is used where the text speaks of the container (its size, its order, a position). |
| frozen, freeze | An `EntityVector` is filled, then frozen by `freeze()`, then read; the two parts are its filling phase and its reading phase. |
| `Ref` | A reference to one entity of a frozen `EntityVector`; an empty `Ref` refers to none. |
| column | A type declared by `DREAMOR_ENTITY_COLUMN` or `DREAMOR_VALUE_COLUMN`. |
| entity column, value column | A column that holds a `Ref` to an entity; a column that holds a plain value that identifies the row by `==`, such as an integer, an enum or a text. |
| text | A `std::string_view` or a `std::string`; a text column holds one of them. |
| fill | A column is filled with a value: from its constructor, a source, or an interval. |
| row | One element of a set: a value for each of its columns. |
| set | An `IndexSet`: rows over named columns, none twice, in a fixed order. |
| range of rows | A range of `std::tuple`s of distinct columns, which a step or a generator takes where it takes a set. |
| key, lookup | A key is the column values that identify a row of a set or an entity of an `EntityVector`; a lookup finds the row or the entity by its key. |
| source | What a step or a generator reads rows or values from: a container, an `EntityVector`, an interval, a set, a range of rows, values in braces, or a column callable. |
| temporary container | A container passed as an rvalue, or values written in braces: it lasts until the end of the statement that creates it. |
| chain, step | A chain is what `index_set::relational::from` returns; a step is one of its member calls (`cross`, `join`, `lateral`, `where`). |
| rows so far | The rows that a chain's earlier steps, or a set's earlier generators, describe; a step or a generator adds to them or narrows them. |
| generator, head | In set-builder notation, a generator is an `in<...>(source)`; the head is the column list of `of<...>`. |
| lower | `of` lowers set-builder notation onto a chain: each generator becomes a step. |
| bind, bound | A step or generator binds a column when it adds the column to the rows; the column is then bound for every step or generator after it. |
| visit order | The order in which `build()` reads the sources; it decides the order of the rows. |
| condition | What keeps or drops a row: a column callable given to `where`, or a generator over a column that is already bound. |
| column callable | A callable whose parameters are columns. |
| name | The text dreamor makes for a row or a variable, from the columns' values. |
| does not compile | A misuse rejected at compile time: a `static_assert`, or a constraint that no overload meets. |
| contract | A precondition that a debug build checks (`@contract`). |

Words that stand in for these, or for plain English, are replaced:

| Instead of | Write |
|---|---|
| stops at compile time | does not compile |
| the row the key names | the row that the key identifies |
| gives, gives back (a function's result) | returns |
| takes (a callable's parameters) | reads, has parameters of |
| brings, starts (a column) | adds |
| used as one expression; named (a chain) | an rvalue; an lvalue |
| either side of `freeze()` | both before and after `freeze()` |
| is welcome | is permitted |

<!-- Copied from docs/guide/names-and-logging.md (blob e82cc272c575), tests/docs/guide/names_and_logging.cpp (blob efc13fdd8f0b) by tools/sync_skill_docs.py - edit the source, not this copy. -->

# Names and logging

A solver names each variable and each row of a model, and the names are what you read in the solver's log and in a
model file that it writes. dreamor makes a name from a row's values by one rule, `<family>_<key0>_<key1>...`, such as
`Hours_M1_2` for M1's hours in period 2. A `VarTable` names its variables by the same rule, as
[Variable families](variable-families.md) shows. dreamor's logger writes messages one line at a time: dreamor's own
messages, and the ones that your model writes.

The examples use the [guide's model](index.md), a plant whose machines make items over three periods:

- `items` holds the items: the frame, the plate, the bolt, the nut and the bracket.
- `machines` holds the machines M1 and M2.
- `cMachine`, `cItem` and `cPeriod` are the columns for a machine, an item and a period. A period is an `int`.

[Entities](entities.md) shows the items and the machines, and [Columns](columns.md) declares the columns.

## Name a row by its keys

Your model gives the solver a row for each machine in each period, to hold the machine's hours. To name such a row
the way a `VarTable` names its variables, call `indexedName` with a family name and the row's columns:

```cpp
const cMachine m{machines.at("M1")};
const cPeriod t{2};
const char* name = dreamor::indexedName("Hours", m, t);  // Hours_M1_2
```

`dreamor::indexedName("Hours", m, t)` joins the family name and each key with `_`. It writes the machine as its
primary key, and the period as its number.

`name` points to `Hours_M1_2`. The name is in a buffer that belongs to the calling thread, and the next call of
`indexedName` on the thread writes over it. So pass the pointer at once to a solver's function that copies the name,
such as the one that names a row.

For how each kind of value is written, and the longest name, see [`indexedName`](../reference/names.md#indexedname).

## Use text, a `Ref` or an enum as a key part

A key part need not be a column of a set. Suppose that your model buys bolts from a supplier, which your data names
by a text, and that a machine's overtime is planned for each shift, an enum. `Shift` is this page's own type, not
part of the model:

```cpp
enum class Shift { kDay, kNight };  // no std::formatter of its own: names as its number
```

```cpp
const std::string supplier = "acme";
const std::string buy      = dreamor::indexedName("Buy", supplier, items.at("bolt"));  // Buy_acme_bolt
const std::string overtime = dreamor::indexedName("Overtime", m, Shift::kNight, t);    // Overtime_M1_1_2
```

- `dreamor::indexedName("Buy", supplier, items.at("bolt"))` writes the text as it is, and the `Ref` as its item's
  primary key.
- `dreamor::indexedName("Overtime", m, Shift::kNight, t)` writes the enum as its number, because `Shift` has no
  `std::formatter` of its own.
- Each `std::string` copies its name out of the thread's buffer.

`buy` is `Buy_acme_bolt`, and `overtime` is `Overtime_M1_1_2`: `Shift::kNight` is 1. `buy` still holds its name after
the second call writes over the buffer, because `buy` holds a copy.

For every kind of value that a name may hold, such as an enum that has a `std::formatter` of its own, see
[Names](../reference/names.md). [Give an entity a composite key](entities.md#give-an-entity-a-composite-key) shows how
an entity with a key of several parts is written.

## Write a name of another shape

Suppose that your solver's file, or your log, wants a name of another shape, such as `Make[M1,frame,2]`, or columns
of a fixed width. Write it with `std::format`, which writes a column as the value that it holds:

```cpp
const std::string make = std::format("Make[{},{},{}]", m, i, t);  // Make[M1,frame,2]
const std::string line = std::format("{:<8}{:>4}", i, t);         // "frame      2"
```

- `std::format("Make[{},{},{}]", m, i, t)` writes the machine and the item as their names, and the period as its
  number.
- `{:<8}` pads the item's name with spaces on the right, to 8 characters. `{:>4}` puts the period's number at the
  right of 4 characters.

`make` is `Make[M1,frame,2]`. `line` is `frame      2`: the 5 letters of `frame` and 3 spaces, then 3 spaces and the
`2`.

For how a format spec applies to each kind of column, see the introduction of [Names](../reference/names.md).

## Name rows in a buffer of your own

Suppose that your model names its rows in a shape of its own, such as `Capacity[M1,1]` for M1's hours in period 1,
and names many of them. `std::format` returns a new `std::string` for each name. A `FormatBuffer` writes each name
into one buffer that the thread keeps, and the next name writes over it. Derive a class from `FormatBuffer`, and give
it a member that makes the name:

```cpp
class RowNames : dreamor::FormatBuffer<64> {  // inherited privately: format() is for its members
 public:
  [[nodiscard]] static const char* capacity(cMachine m, cPeriod t) { return format("Capacity[{},{}]", m, t); }
};

```

```cpp
const cMachine m1{machines.at("M1")};
const char* name = RowNames::capacity(m1, 1);  // Capacity[M1,1]
```

- `RowNames` derives from `dreamor::FormatBuffer<64>`, a buffer of 64 characters. The base is private, so only the
  members of `RowNames` call `format`.
- `format` takes a format string and its arguments, as `std::format` does, writes the name into the buffer, and
  returns a pointer to it.
- `format` is a static member function, so `capacity`, which uses nothing else, is static too.

`name` points to `Capacity[M1,1]`. The next call of `capacity` on the thread writes over it, so pass the pointer at
once to a function that copies the name.

For the buffer's rules, see [`FormatBuffer`](../reference/names.md#formatbuffer). For what happens to a name that is
too long for the buffer, see [`OnFormatOverflow`](../reference/names.md#onformatoverflow).

## Log a model's own messages

Your model can say what it built, and warn about data that looks wrong, in dreamor's log, beside dreamor's own
messages. `Sets` is a struct of the model's sets, which `byChain` builds as chains: `PRODUCE` holds each machine with
each needed item that it makes, in each period, `PERIODS` holds the periods, and `NEEDED` holds the items that the
plant needs. Write a line at the level that the message has:

```cpp
void logSets(const Sets& sets, const dreamor::EntityVector<Item>& items) {
  DREAMOR_LOG_INFO("{} rows of production over {} periods", sets.PRODUCE.size(), sets.PERIODS.size());
  if (sets.NEEDED.size() < items.size()) {
    DREAMOR_LOG_WARNING("{} of {} items are needed by no one", items.size() - sets.NEEDED.size(), items.size());
  }
  if (sets.PRODUCE.empty()) { DREAMOR_LOG_ERROR("no machine makes an item that is needed"); }
}

```

- `DREAMOR_LOG_INFO` writes an information line. Its arguments are a format string and its arguments, as for
  `std::format`.
- `DREAMOR_LOG_WARNING` writes a warning line, with the same arguments.
- `DREAMOR_LOG_ERROR` writes an error line, with the same arguments.

Over the model's sets, the information line is `15 rows of production over 3 periods`. The warning is
`1 of 5 items are needed by no one`: the bracket, which no customer buys and no sold item takes. The error line is
not written, because `PRODUCE` has rows. Each line starts with the time since the process wrote its first line, the
file and the line of the call, and the level, such as `[INFO]`.

For a line's parts and the levels, see [Logger](../reference/logger.md). To stop the program after an error line,
see [`DREAMOR_LOG_FATAL`](../reference/logger.md#dreamor_log_fatal).

## Choose what is logged and where

Suppose that a run should log only warnings and errors, and write them to a file of its own. Set the level, and send
the thread's lines to the file:

```cpp
dreamor::logger::setLevel(dreamor::logger::Level::kWarning);  // every thread drops info lines from here on
const dreamor::logger::FileSink sink{log_path.c_str(), "w"};  // this thread's lines go to the file while sink lives
logSets(sets, items);
```

- `setLevel(Level::kWarning)` drops every line below a warning, in every thread of the process.
- `FileSink sink{log_path.c_str(), "w"}` opens the file at `log_path` and sends the lines of this thread to it while
  `sink` exists. The mode `"w"` starts the file afresh.
- `logSets(sets, items)`, from the section above, writes its lines.

The file holds the warning line alone. The information line is dropped, and the file holds nothing from before the
run. When `sink` is destroyed, the thread's lines go where they went before. Another thread's lines still go to
`stderr`, but the level holds for that thread too.

For each call, see [`setLevel`](../reference/logger.md#setlevel) and [`FileSink`](../reference/logger.md#filesink).
To send the lines to a file that you keep open yourself, see [`setOutput`](../reference/logger.md#setoutput).

## See also

- [Names](../reference/names.md): how each kind of value is written in a name, and `FormatBuffer`.
- [Logger](../reference/logger.md): a line's parts, the levels, each thread's output, and the macros of a debug
  build.
- [Variable families](variable-families.md): the names that a `VarTable` gives its variables.

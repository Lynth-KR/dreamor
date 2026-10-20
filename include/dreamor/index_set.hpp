// SPDX-License-Identifier: Apache-2.0
// File: index_set.hpp
// Builds the index sets of a mathematical model as read-only IndexSet<std::tuple<...>>,
// the way an AMPL set declaration does:
//
//     {m in MACHINES, i in ITEMS[m]: fits[m,i]}
//     makeSet<MachineCol, ItemCol>(machines, items_of).where(fits).collect()
//
//     {(m,i) in ASSIGN, o in ORDERS: orderFits[i,o]}
//     makeSet<OrderCol>(sASSIGN, orders).where(order_fits).collect()
//
//     {(m,i) in ASSIGN: early[i]}
//     sASSIGN.where(early)
//
// Everything is addressed by column name - `using MachineCol = Col<const Machine*, "machine">;` -
// never by position, and that holds for every callable the DSL takes. A predicate's
// parameters ARE its columns: `[](ItemCol i, OrderCol o)` reads i and o wherever they sit
// and runs as soon as both are bound, so parameter order and .where() order are both free.
// A dependent source is the same - `[](ItemCol i)` names the one column it needs.
//
// Hence the one rule the DSL will not bend on: a callable takes COLUMN types, never the
// values they wrap. `[](const Item* i)` is rejected. A value type does not identify a
// column - two columns over the same entity are ordinary - so accepting it would leave the
// library guessing. Col converts to its value implicitly and -> and * reach the entity, so
// only the parameter list changes.
//
// A dependent source names which of the model's entities apply, so it yields POINTERS to
// them - `std::vector<const Item*>`, a span, a view - never the entities. A range of
// entities is a range of copies, whose addresses are not the ones the rest of the model
// uses.
//
// The intermediate product is never materialized: a column can be a function of the columns
// before it, an existing set can fill the columns it already has, and a rejected partial
// tuple prunes every loop below it. Sources are also visited in whatever order lets the
// predicates prune earliest, which is not always the order they were declared in.
//
// The result is an IndexSet in generation order, which the visit order decides - the
// tuples are always (MachineCol, ItemCol, OrderCol), but which column varies fastest is not
// fixed. Where the row order carries meaning, sort it with orderBy().
//
// An IndexSet is read-only: begin(), end(), size() for range-for and any read-only
// algorithm, and no way to edit it. A different set is derived instead -
// S.where(p), S.orderBy(key), S.distinct(), unite(A, B), ... each returns a new
// set and leaves S as it was. Copies are O(1) and share the rows. Declare sets with
// SetOf<MachineCol, ItemCol>.
//
// It is a SET only as far as its sources are. makeSet never creates a duplicate row, but it
// carries one through from a container that repeats an entity or a dependent source that
// hands back the same pointer twice, and a duplicate row counts twice in every sum over it.
// S.assertUnique() checks that in a debug build; S.distinct() and S.select<...>() collapse it.
//
// Naming predicates is a convention, not a rule: `.where(fits)` keeps the model's own
// vocabulary in the code, but a condition used once reads better inline.
//
// Include this for all of it, or one of the headers below for a part.
#pragma once

#include "dreamor/index_set/algebra.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/grouped.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/ordered.hpp"
#include "dreamor/index_set/set.hpp"

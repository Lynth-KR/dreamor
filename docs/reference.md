# Public API reference

**English** · [한국어](ko/reference.md)

Every public type and function, by header. The [guide](guide.md) explains them in use.

**Columns** (`column.hpp`)
| | |
|---|---|
| `Col<T, "name">` | Column type. `Col(T)` (implicit), `operator T()`, `get()`, `->` and `*` for pointers, `==`, default construction |
| `Col<...>::kName` | Column name (`std::string_view`) |
| `ColumnName` | Type of `Col`'s name template argument (a string literal) |
| `keyName(Col)` | The column's name token. The value's `keyName` if it has one, else an entity's `primaryKey()`, otherwise the value itself |
| `std::formatter<Col<...>>` | A column prints as its name token, so `std::format("Cap_{}_{}", i, j)` names a row the way VarTable names variables. Takes no format spec; a column with no token has none |
| `std::hash<Col<...>>` | Lets a column be a key of `std::unordered_map` and the like |
| `PredicateColumns<F>` | Specialization point declaring the columns read by a function object with a template `operator()` |

**Building sets** (`set.hpp`)
| | |
|---|---|
| `makeSet<Cols...>(sources...)` | Set builder |
| Builder `.where(p)` | Adds one predicate (call repeatedly for several) |
| Builder `.collect()` | Finalizes into an `IndexSet` |

**Sets** (`indexed.hpp`, implemented in `set.hpp`/`algebra.hpp`/`grouped.hpp`/`ordered.hpp`)
| | |
|---|---|
| `IndexSet<std::tuple<Cols...>>`, alias `SetOf<Cols...>` | Read-only set. Default construction (empty set), O(1) copy |
| `begin()`, `end()`, `size()` | Random-access iteration |
| `where(p...)`, `orderBy(key)`, `select<Cols...>()`, `select(expr)` | Derivation → `IndexSet` |
| `groupBy<Cols...>()` | → `Grouped` |
| `membership()` | → `Membership` |
| `ordered()` | → `Ordered` |
| `unite(A, B)`, `intersect(A, B)`, `difference(A, B)` | Binary operations → `IndexSet` |

**EntityVector** (`entity_vector.hpp`)
| | |
|---|---|
| `primaryKey(const T&)` | ADL hook next to `T`: what identifies it (a value or a `std::tuple`). `dreamor::Entity<T>` checks for it |
| `insert(entity)`, `emplace(args...)` | `{iterator to the element with that key, whether it went in}` |
| `find(key)`, `contains(key)` | By primary key |
| `operator[]`, `at`, `front`, `back`, `begin`, `end`, `cbegin`, `cend`, `size`, `empty` | Const access, insertion order |
| `value_type`, `key_type`, `size_type`, `const_iterator` (= `iterator`) | Member types, named as the standard containers name them; `key_type` is what `primaryKey()` returns |
| `modify(entity, fn)` | `fn(T&)` on one element; its key must not change |

**Membership** (`algebra.hpp`)
| | |
|---|---|
| `(cols...)` / `(row)` | Membership as `bool` |

**Grouped** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()` | Every existing group (in first-occurrence order). The element `Group` unpacks as `[cols..., rows]` |
| `Key`, `Group{key, tuples}` | Group key tuple, one group |
| `slice(cols...)`, `slice(key)` | That key's `RowSlice` (empty if absent). `key` comes from `keyAt()` or `Group::key` |
| `contains(cols...)`, `sliceSize(cols...)` | Key existence, group size (also take `key`) |
| `size()`, `empty()` | Number of groups |
| `keyAt(g)`, `at(g)` | Key of the g-th group, the group (`std::out_of_range` if out of range) |
| `regrouped()` | A new `IndexSet` with rows gathered group by group |

**RowSlice** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()`, `size()`, `empty()`, `operator[](i)`, `front()`, `back()` | Random-access view, default construction (empty) |

**Ordered** (`ordered.hpp`)
| | |
|---|---|
| `ord(cols...)` | Position (`std::optional<std::size_t>`, empty if absent). Everything below also takes a single row instead of columns |
| `next(cols...)`, `next(cols..., steps)`, `prev(...)` likewise | Pointer to the neighboring row, or `nullptr` |
| `nextCyclic(...)`, `prevCyclic(...)` | Neighbors that wrap from the end back to the start |
| `first()`, `last()`, `at(pos)` | First, last, by position |
| `isFirst(cols...)`, `isLast(cols...)`, `contains(cols...)` | Tests |
| `size()`, `empty()`, `members()` | Size, the deduplicated set |

**Variables** (`concert.hpp`, namespace `dreamor::concert`)
| | |
|---|---|
| `VarTable<Keys...>{env, set, lb = 0, ub = IloInfinity, type = ILOFLOAT, name = nullptr}` | lb, ub are numbers or column functions |
| `VarTable<Keys...>{semiContinuous, env, set, sclb, ub, type, name}` | Semi-continuous |
| `Key` | `std::tuple<Keys...>` |
| `(keys...)`, `(row)` | The variable: `vYield(m, i, t)`, `vYield(row)` (`std::out_of_range` if absent, common to all backends) |
| `contains(keys...)`, `contains(row)`, `size()`, `keys()` | |
| `begin()`, `end()` | `(row, var)` iteration |
| `solution(algorithm)` | → `ValueTable` |
| `SemiContinuous`, `semiContinuous` | Tag for the semi-continuous constructor |

**Values** (`value_table.hpp`, namespace `dreamor`, common to all backends)
| | |
|---|---|
| `ValueTable<Keys...>` | Values by `(keys...)`/`(row)`: `yieldPlan(m, i, t)` (`std::out_of_range` if absent), `contains`, `size()`, `keys()`, `(row, value)` iteration |

**Sums** (`concert.hpp`, namespace `dreamor::concert`)
| | |
|---|---|
| `sumOf(env, rows, term)`, `sumOf<Col>(env, container, term)` | Sum builder. The term is a column function or a `VarTable` itself |
| Builder `.where(p...)`, `.split(p...)`, `.build()` | Filter, split sums, `IloExpr` or `std::array<IloExpr, N>` |

**MathOpt backend** (`mathopt.hpp`, namespace `dreamor::mathopt`)
| | |
|---|---|
| `VarTable<Keys...>{model, set, lb = 0, ub = +inf, is_integer = false, name = nullptr}` | `model` is `math_opt::Model&`; lb, ub are numbers or column functions |
| `(keys...)`, `(row)`, `contains`, `size`, `keys`, `begin`/`end` | Same as Concert (variables are `math_opt::Variable`) |
| `solution(result)` | `math_opt::SolveResult` → `ValueTable`; `std::logic_error` if there is no feasible solution |
| `sumOf(rows, term)`, `sumOf<Col>(container, term)` | Sum builder; `build()` gives `math_opt::LinearExpression` (an array of them with `split`) |


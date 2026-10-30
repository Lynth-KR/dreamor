# 공개 API 전체 목록

[English](../reference.md) · **한국어**

공개된 타입과 함수 전부를 헤더별로 정리했습니다. 실제로 쓰는 법은 [가이드](guide.md)에 있습니다.

**열** (`column.hpp`)
| | |
|---|---|
| `Col<T, "name">` | 열 타입. `Col(T)`(암묵), `operator T()`, `get()`, 포인터면 `->`, `*`, `==`, 기본 생성 |
| `Col<...>::kName` | 열 이름 (`std::string_view`) |
| `ColumnName` | `Col`의 이름 템플릿 인자 타입 (문자열 리터럴) |
| `keyName(Col)` | 열의 이름 토큰. 값에 `keyName`이 있으면 그것, 없으면 엔티티의 `primaryKey()`, 그것도 없으면 값 자체 |
| `std::formatter<Col<...>>` | 열을 이름 토큰으로 출력. `std::format("Cap_{}_{}", i, j)`로 VarTable이 변수 이름을 짓듯 행 이름을 지을 수 있음. 서식 지정은 받지 않음. 토큰이 없는 열에는 없음 |
| `std::hash<Col<...>>` | 열을 `std::unordered_map` 키 등으로 쓸 수 있음 |
| `PredicateColumns<F>` | 템플릿 `operator()` 함수 객체가 읽는 열을 선언하는 특수화 지점 |

**집합 만들기** (`set.hpp`)
| | |
|---|---|
| `makeSet<Cols...>(sources...)` | 집합 빌더 |
| 빌더 `.where(p)` | 술어 하나 추가 (여러 개면 이어서 호출) |
| 빌더 `.collect()` | `IndexSet`으로 확정 |

**집합** (`indexed.hpp`, 구현은 `set.hpp`/`algebra.hpp`/`grouped.hpp`/`ordered.hpp`)
| | |
|---|---|
| `IndexSet<std::tuple<Cols...>>`, 별칭 `SetOf<Cols...>` | 읽기 전용 집합. 기본 생성(빈 집합), 복사 O(1) |
| `begin()`, `end()`, `size()` | random access 순회 |
| `where(p...)`, `orderBy(key)`, `select<Cols...>()`, `select(expr)` | 파생 → `IndexSet` |
| `groupBy<Cols...>()` | → `Grouped` |
| `membership()` | → `Membership` |
| `ordered()`, `ordered<A...>()` | → 집합 전체, 또는 `A...`가 같은 행끼리의 그룹마다의 `Ordered` |
| `unite(A, B)`, `intersect(A, B)`, `difference(A, B)` | 이항 연산 → `IndexSet` |
| `A == B`, `A != B` | 같은 행, 순서 무관 |

**EntityVector** (`entity_vector.hpp`)
| | |
|---|---|
| `primaryKey(const T&)` | `T` 옆의 ADL 훅: 무엇이 그것을 식별하는지(값 또는 `std::tuple`). `dreamor::Entity<T>`가 이를 확인 |
| `insert(entity)`, `emplace(args...)` | `std::pair<const T*, bool>`: `{그 키의 원소의 주소, 들어갔는지}` |
| `find(key)` | `const T*`: 그 키의 원소, 없으면 `nullptr` |
| `contains(key)` | 그 키의 원소가 있는지 |
| `operator[]`, `at`, `front`, `back`, `begin`, `end`, `cbegin`, `cend`, `size`, `empty` | const 접근, 삽입 순서 |
| `value_type`, `key_type`, `size_type`, `const_iterator` (= `iterator`) | 멤버 타입. 표준 컨테이너와 같은 이름이며, `key_type`은 `primaryKey()`가 돌려주는 타입 |
| `modify(entity, fn)` | 원소 하나에 `fn(T&)`; 키는 바뀌면 안 됨 |

**Membership** (`algebra.hpp`)
| | |
|---|---|
| `(열...)` / `(행)` | 소속 여부 `bool` |

**Grouped** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()` | 존재하는 묶음 전부 (첫 등장 순). 원소 `Group`은 `[열..., rows]`로 풀림 |
| `Key`, `Group{key, tuples}` | 묶음 키 튜플, 묶음 하나 |
| `slice(열...)`, `slice(key)` | 그 키의 `RowSlice` (없으면 빈 것). `key`는 `keyAt()`이나 `Group::key`에서 |
| `contains(열...)`, `sliceSize(열...)` | 키 존재, 묶음 크기 (`key`도 받음) |
| `size()`, `empty()` | 묶음 수 |
| `keyAt(g)`, `at(g)` | g번째 묶음의 키, 묶음 (범위 밖이면 `std::out_of_range`) |
| `regrouped()` | 묶음별로 모은 새 `IndexSet` |

**RowSlice** (`grouped.hpp`)
| | |
|---|---|
| `begin()`, `end()`, `size()`, `empty()`, `operator[](i)`, `front()`, `back()` | random access 뷰, 기본 생성(빈 것) |

**Ordered** (`ordered.hpp`)
| | |
|---|---|
| `ord(열...)` | 위치 (`std::optional<std::size_t>`, 없으면 비어 있음). 아래 모두 열 대신 행 하나도 받음 |
| `next(열...)`, `next(열..., steps)`, `prev(...)` 같은 꼴 | 이웃 행 포인터 또는 `nullptr` |
| `nextCyclic(...)`, `prevCyclic(...)` | 끝에서 처음으로 돌아가는 이웃 |
| `first()`, `last()`, `at(pos)` | 처음, 끝, 위치로 (`ordered<A...>()`에는 없음. 거기서 `ord`, `isFirst`, `isLast`는 그룹 안 기준) |
| `isFirst(열...)`, `isLast(열...)`, `contains(열...)` | 판정 |
| `size()`, `empty()`, `members()` | 크기, 중복 제거된 집합 |

**변수** (`concert.hpp`, 네임스페이스 `dreamor::concert`)
| | |
|---|---|
| `VarTable<Keys...>{env, set, lb = 0, ub = IloInfinity, type = ILOFLOAT, name = nullptr}` | lb, ub는 숫자 또는 열 함수 |
| `VarTable<Keys...>{semiContinuous, env, set, sclb, ub, type, name}` | 반연속 |
| `Key` | `std::tuple<Keys...>` |
| `(keys...)`, `(row)` | 변수: `vYield(m, i, t)`, `vYield(row)` (없으면 `std::out_of_range`, 모든 백엔드 공통) |
| `contains(keys...)`, `contains(row)`, `size()`, `keys()` | |
| `begin()`, `end()` | `(row, var)` 순회 |
| `solution(algorithm)` | → `ValueTable` |
| `SemiContinuous`, `semiContinuous` | 반연속 생성자 태그 |

**값** (`value_table.hpp`, 네임스페이스 `dreamor`, 백엔드 공통)
| | |
|---|---|
| `ValueTable<Keys...>` | `ValueTable<Keys...>{set, value}`: 집합 위의 데이터(`param {S}`), `value`는 숫자나 열 콜러블. ValueTable 자체도 열 콜러블(bound, 값). `(keys...)`/`(row)`로 값: `yieldPlan(m, i, t)` (없으면 `std::out_of_range`), `contains`, `size()`, `keys()`, `(row, value)` 순회 |

**합** (`concert.hpp`, 네임스페이스 `dreamor::concert`)
| | |
|---|---|
| `sumOf(env, rows, term)`, `sumOf<Col>(env, container, term)` | 합 빌더. term은 열 함수 또는 `VarTable` 자체 |
| 빌더 `.where(p...)`, `.split(p...)`, `.build()` | 거르기, 나눠 합, `IloExpr` 또는 `std::array<IloExpr, N>` |

**MathOpt 백엔드** (`mathopt.hpp`, 네임스페이스 `dreamor::mathopt`)
| | |
|---|---|
| `VarTable<Keys...>{model, set, lb = 0, ub = +inf, is_integer = false, name = nullptr}` | `model`은 `math_opt::Model&`; lb, ub는 숫자 또는 열 함수 |
| `(keys...)`, `(row)`, `contains`, `size`, `keys`, `begin`/`end` | Concert와 같음 (변수는 `math_opt::Variable`) |
| `solution(result)` | `math_opt::SolveResult` → `ValueTable`; 가능해가 없으면 `std::logic_error` |
| `sumOf(rows, term)`, `sumOf<Col>(container, term)` | 합 빌더; `build()`는 `math_opt::LinearExpression`, term이 이차 항이면 `math_opt::QuadraticExpression` (`split`이면 그 배열) |

## 유틸리티

**이름** (`general/format_buffer.hpp`, 네임스페이스 `dreamor::general`)
| | |
|---|---|
| `exprName(family, keys...)` (`index_set/name_token.hpp`, 네임스페이스 `dreamor`) | `"<family>_<key0>_<key1>..."`, `VarTable`이 변수 이름을 짓듯 지은 행 이름: 키는 열(그 토큰)이나 값. 스레드의 버퍼에서 만들어 할당도 서식 문자열도 없음. `const char*`는 같은 스레드의 다음 `exprName`까지 유효 (복사해 가는 `setName`에 바로 넘김). 255글자를 넘으면 `std::runtime_error` |
| `NameBuilder<N, Policy = OnFormatOverflow::kThrow>` | `N`글자 자기 버퍼에 이름을 조각조각 만듦: `NameBuilder{root}`나 `restart(root)`, 그다음 토큰마다 `append(length, token, separator = "_")` (숫자는 `std::to_chars`, 문자열은 복사, 그 밖은 서식), `name()`. `VarTable`이 변수 이름을, `exprName`이 행 이름을 이것으로 지음 |
| `FormatBuffer<N, Policy = OnFormatOverflow::kThrow>` | 이름용 고정 버퍼(`N`글자). 스레드마다 하나라서 객체는 아무것도 들고 있지 않음. private으로 상속하고 `format(fmt, args...)`을 부름. `format`은 static이라 `const` 멤버 함수에서도, 여러 스레드에서 동시에도 부를 수 있음. 돌려주는 `const char*`는 같은 스레드에서 같은 `FormatBuffer<N, Policy>`를 통한 다음 `format`까지 유효 (복사해 가는 `setName`에 바로 넘김). 열은 이름 토큰으로 출력 |
| `OnFormatOverflow::kThrow`, `kLogAndClear` | 이름이 `N - 1`글자를 넘으면: `std::runtime_error`, 또는 경고와 빈 이름 (이때 `format`은 `noexcept`) |

**로그** (`logger/logger_light.hpp`)
| | |
|---|---|
| `DREAMOR_LOG_INFO(fmt, args...)`, `DREAMOR_LOG_WARNING`, `DREAMOR_LOG_ERROR` | 한 줄: `[+경과 ms] [파일:줄] [수준] 본문`. 본문은 `std::format` (컴파일 시점 검사). 터미널이면 색을 입힘 (`NO_COLOR`가 있으면 끔). `ERROR`는 바로 flush |
| `DREAMOR_LOG_FATAL(fmt, args...)` | 기록하고 flush한 뒤 중단. 모든 빌드에서 |
| `DREAMOR_DEBUG_INFO`, `DREAMOR_DEBUG_ERROR`, `DREAMOR_DEBUG_FATAL` | 디버그 빌드에서만 같은 동작. `NDEBUG`에서는 인자까지 사라짐 |
| `logger::setLevel(Level)`, `logger::getLevel()` | `Level::kInfo` / `kWarning` / `kError`보다 낮은 줄은 포매팅 전에 버림. 프로세스 전체 |
| `logger::setOutput(FILE*)`, `logger::getOutput()`, `logger::flush()` | 호출한 스레드의 출력 대상 (기본 `stderr`) |
| `logger::FileSink{path, mode = "a"}` | 범위가 끝날 때까지 호출한 스레드의 로그를 `path`로 보내고, 끝나면 이전 대상으로 되돌림 |
| `DREAMOR_LOGGER_ENABLE_TIMESTAMP` | include 전에 `0`으로 정의하면 경과 시간 접두어를 뺌 |

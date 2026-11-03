# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Compares two MPS files as LPs, by what they mean rather than how a writer spells them: the same named rows and
columns, each column's bounds, integrality and cost, each row's bounds and coefficients, every number within a
relative 1e-12. A row may be written either way round - a >= b as a - b >= 0 or as b - a <= 0 - and a writer may
leave out a zero right-hand side or a default bound, or name the objective as it likes; none of that is a
difference. The quadratic objective (QMATRIX, QUADOBJ) and the indicator constraints (INDICATORS) are compared too.
Prints what differs, and the largest relative difference; exits 1 on a difference.

A writer names what the model left unnamed by its position - CPLEX's x123 for a column, c45 for a row, i67 for an
indicator constraint - so the same model built in another order gets other names there. --made-up REGEX says which
names are made up: each of those columns and rows is then known by what it holds - its bounds, integrality, cost and
coefficients, with every made-up name in them masked - and the two files must hold the same of them, in any order.

    python3 bench/common/compare_mps.py dreamor.mps hand.mps
    python3 bench/common/compare_mps.py --made-up '[xci][0-9]+' before.mps after.mps
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

TOLERANCE = 1e-12
INF = math.inf


@dataclass
class Lp:
    columns: dict[str, list] = field(default_factory=dict)  # name -> [lower, upper, integer, cost]
    rows: dict[str, list[float]] = field(default_factory=dict)  # name -> [lower, upper]
    entries: dict[str, dict[str, float]] = field(default_factory=dict)  # row -> column -> coefficient
    quadratic: dict[tuple[str, str], float] = field(default_factory=dict)  # (column, column), in name order -> value
    indicators: dict[str, tuple[str, float]] = field(default_factory=dict)  # row -> (binary column, value it holds at)


def parse(path: str) -> Lp:
    lp = Lp()
    sense: dict[str, str] = {}
    objective = None
    section, in_integers = None, False
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("*"):
            continue
        if not line[0].isspace():
            section = line.split()[0]
            continue
        fields = line.split()
        if section == "ROWS":
            kind, name = fields
            if kind == "N" and objective is None:
                objective = name
            elif kind != "N":
                sense[name] = kind
                lp.rows[name] = [0.0, INF] if kind == "G" else [-INF, 0.0] if kind == "L" else [0.0, 0.0]
                lp.entries[name] = {}
        elif section == "COLUMNS":
            if "'MARKER'" in fields or "MARKER" in fields:
                in_integers = "'INTORG'" in fields
                continue
            column = fields[0]
            entry = lp.columns.setdefault(column, [0.0, INF, in_integers, 0.0])
            for row, value in zip(fields[1::2], fields[2::2], strict=True):
                if row == objective:
                    entry[3] = float(value)
                elif row in lp.entries:
                    lp.entries[row][column] = float(value)
        elif section == "RHS":
            pairs = fields[1:] if len(fields) % 2 else fields  # after the vector's name, when it has one
            for row, value in zip(pairs[0::2], pairs[1::2], strict=True):
                if row in lp.rows:
                    bounds = lp.rows[row]
                    if sense[row] in ("G", "E"):
                        bounds[0] = float(value)
                    if sense[row] in ("L", "E"):
                        bounds[1] = float(value)
        elif section == "RANGES":
            pairs = fields[1:] if len(fields) % 2 else fields
            for row, value in zip(pairs[0::2], pairs[1::2], strict=True):
                low, high = lp.rows[row]
                r = abs(float(value))
                lp.rows[row] = [low, low + r] if sense[row] == "G" else [high - r, high]
        elif section == "BOUNDS":
            kind, column = fields[0], fields[2]
            value = float(fields[3]) if len(fields) > 3 else None
            bounds = lp.columns.setdefault(column, [0.0, INF, False, 0.0])
            match kind:
                case "UP" | "UI":
                    bounds[1] = value
                case "LO" | "LI":
                    bounds[0] = value
                case "FX":
                    bounds[0] = bounds[1] = value
                case "FR":
                    bounds[0], bounds[1] = -INF, INF
                case "MI":
                    bounds[0] = -INF
                case "PL":
                    bounds[1] = INF
                case "BV":
                    bounds[0], bounds[1], bounds[2] = 0.0, 1.0, True
        elif section in ("QMATRIX", "QUADOBJ"):
            first, second, value = fields
            pair = (min(first, second), max(first, second))
            # QMATRIX holds both halves of an off-diagonal term, QUADOBJ one: the sum is the term either way
            lp.quadratic[pair] = lp.quadratic.get(pair, 0.0) + float(value) * (
                2 if section == "QUADOBJ" and first != second else 1
            )
        elif section == "INDICATORS":
            _, row, column, value = fields
            lp.indicators[row] = (column, float(value))
    return lp


class Comparison:
    def __init__(self) -> None:
        self.mismatches = 0
        self.worst = 0.0

    def number(self, what: str, a: float, b: float) -> None:
        if a == b:
            return
        off = abs(a - b) / max(abs(a), abs(b), 1.0) if math.isfinite(a) and math.isfinite(b) else INF
        self.worst = max(self.worst, off)
        if off > TOLERANCE:
            self.mismatches += 1
            if self.mismatches <= 5:
                print(f"{what}: {a!r} vs {b!r}")

    def missing(self, what: str, only_first: set, only_second: set) -> None:
        if only_first or only_second:
            self.mismatches += len(only_first) + len(only_second)
            print(f"{what}: {len(only_first)} only in the first, {len(only_second)} only in the second")


def anonymous(lp: Lp, made_up: re.Pattern) -> tuple[Lp, Counter]:
    """lp without its made-up columns and rows, and those as a multiset of what each holds, made-up names masked."""
    mask = lambda name: "?" if made_up.fullmatch(name) else name  # noqa: E731
    number = lambda v: f"{v:.12g}"  # noqa: E731
    held: Counter = Counter()
    by_column: dict[str, list] = {}
    for row, entries in lp.entries.items():
        for column, value in entries.items():
            by_column.setdefault(column, []).append((mask(row), number(value)))
    for column, (low, up, integer, cost) in lp.columns.items():
        if made_up.fullmatch(column):
            held[
                "column", number(low), number(up), integer, number(cost), tuple(sorted(by_column.get(column, [])))
            ] += 1
    for row, (low, up) in lp.rows.items():
        if made_up.fullmatch(row):
            entries = sorted((mask(c), v) for c, v in lp.entries[row].items())
            if entries and entries[0][1] < 0:  # written the other way round
                entries, low, up = [(c, -v) for c, v in entries], -up, -low
            held["row", number(low), number(up), tuple((c, number(v)) for c, v in entries)] += 1
    for (first, second), value in lp.quadratic.items():
        if made_up.fullmatch(first) or made_up.fullmatch(second):
            held["quadratic", *sorted((mask(first), mask(second))), number(value)] += 1
    for row, (column, value) in lp.indicators.items():
        if made_up.fullmatch(row) or made_up.fullmatch(column):
            held["indicator", mask(row), mask(column), number(value)] += 1
    named = Lp(
        columns={k: v for k, v in lp.columns.items() if not made_up.fullmatch(k)},
        rows={k: v for k, v in lp.rows.items() if not made_up.fullmatch(k)},
        entries={
            k: {c: x for c, x in v.items() if not made_up.fullmatch(c)}
            for k, v in lp.entries.items()
            if not made_up.fullmatch(k)
        },
        quadratic={k: v for k, v in lp.quadratic.items() if not (made_up.fullmatch(k[0]) or made_up.fullmatch(k[1]))},
        indicators={k: v for k, v in lp.indicators.items() if not (made_up.fullmatch(k) or made_up.fullmatch(v[0]))},
    )
    for row, entries in lp.entries.items():  # a named row's coefficients on made-up columns
        if not made_up.fullmatch(row):
            masked = sorted(number(v) for c, v in entries.items() if made_up.fullmatch(c))
            if masked:
                held["coefficients", row, tuple(masked)] += 1
    return named, held


def main(first: str, second: str, made_up: str | None) -> int:
    a, b = parse(first), parse(second)
    c = Comparison()
    if made_up:
        pattern = re.compile(made_up)
        (a, held_a), (b, held_b) = anonymous(a, pattern), anonymous(b, pattern)
        differ = (held_a - held_b) + (held_b - held_a)
        c.mismatches += sum(differ.values())
        if differ:
            print(f"made-up names: {sum(differ.values())} columns, rows or terms held by one file and not the other")
        print(f"made-up names: {sum(held_a.values())} columns, rows and terms compared by what they hold")
    c.missing("columns", a.columns.keys() - b.columns.keys(), b.columns.keys() - a.columns.keys())
    for name in a.columns.keys() & b.columns.keys():
        (low_a, up_a, int_a, cost_a), (low_b, up_b, int_b, cost_b) = a.columns[name], b.columns[name]
        c.number(f"{name} lower", low_a, low_b)
        c.number(f"{name} upper", up_a, up_b)
        c.number(f"{name} cost", cost_a, cost_b)
        if int_a != int_b:
            c.mismatches += 1
    c.missing("rows", a.rows.keys() - b.rows.keys(), b.rows.keys() - a.rows.keys())
    coefficients = 0
    for row in a.rows.keys() & b.rows.keys():
        ea, eb = a.entries[row], b.entries[row]
        c.missing(f"row {row}'s columns", ea.keys() - eb.keys(), eb.keys() - ea.keys())
        shared = ea.keys() & eb.keys()
        sign = -1.0 if any(ea[k] * eb[k] < 0 for k in list(shared)[:1]) else 1.0
        for column in shared:
            c.number(f"{row} {column}", ea[column], sign * eb[column])
        coefficients += len(ea)
        low, up = b.rows[row]
        c.number(f"{row} lower", a.rows[row][0], low if sign > 0 else -up)
        c.number(f"{row} upper", a.rows[row][1], up if sign > 0 else -low)
    c.missing("quadratic terms", a.quadratic.keys() - b.quadratic.keys(), b.quadratic.keys() - a.quadratic.keys())
    for pair in a.quadratic.keys() & b.quadratic.keys():
        c.number(f"quadratic {pair}", a.quadratic[pair], b.quadratic[pair])
    c.missing(
        "indicator constraints", a.indicators.keys() - b.indicators.keys(), b.indicators.keys() - a.indicators.keys()
    )
    for row in a.indicators.keys() & b.indicators.keys():
        if a.indicators[row][0] != b.indicators[row][0]:
            c.mismatches += 1
        c.number(f"indicator {row}", a.indicators[row][1], b.indicators[row][1])
    print(
        f"{coefficients} coefficients, {len(a.rows)} rows, {len(a.columns)} columns; "
        f"mismatches beyond {TOLERANCE:g}: {c.mismatches}; largest relative difference: {c.worst:.3g}"
    )
    return 1 if c.mismatches else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("first")
    parser.add_argument("second")
    parser.add_argument("--made-up", metavar="REGEX", help="names a writer made up, compared by what they hold")
    args = parser.parse_args()
    sys.exit(main(args.first, args.second, args.made_up))

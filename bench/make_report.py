#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""run_doe.py results CSV -> analysis workbook.

  python3 make_report.py results.csv report.xlsx

Every summary, effect and extrapolation in the workbook is a formula over the Raw sheet: add
or fix rows in Raw and it recalculates. Python writes only the design labels and the raw
measurements as values.

Sheets
  Summary   headline ratios, extrapolation, check result
  Design    factors and levels, arms, responses, fairness notes, machine
  Cells     16 cells x arms: geometric means, ratios, variances per response
  Effects   main effects and two-factor interactions on ln(ratio), standard errors, t
  Scale     scale-ladder fits (time: log-log, memory: linear) and the prediction at N*
  Check     whether the arms handed out the same variables in the same groups, per model
  Charts    ratio bars per cell, scale ladder
  Raw       measurements + derived columns (formulas)
"""

import csv
import itertools
import json
import os
import sys

from openpyxl import Workbook
from openpyxl.chart import BarChart, Reference, ScatterChart, Series
from openpyxl.comments import Comment
from openpyxl.styles import Alignment, Font, PatternFill
from openpyxl.utils import get_column_letter as L

FONT = "Arial"
BASE = "dreamor"
IMPLS = ["dreamor", "map", "flat"]
INAME = {
    "dreamor": "dreamor",
    "map": "std::map (hand-written)",
    "flat": "flat arrays (hand-optimized)",
}
FACTORS = [
    ("W", "workload", "prefix", "mixed"),
    ("O", "K2 owner", "shared", "owned"),
    ("D", "density", "dense", "sparse"),
    ("S", "fan-out", "uniform", "skewed"),
]
RESPONSES = [  # key, name, ln column in Raw, unit, definition
    (
        "build",
        "model build",
        "ln_build",
        "ns/row",
        "rows + container + variables + groupings, per row",
    ),
    (
        "group",
        "group sums",
        "ln_group",
        "ns/visit",
        "one expression per group, per (row x groupings)",
    ),
    (
        "hit",
        "point lookup",
        "ln_hit",
        "ns/lookup",
        "one lookup of an existing key, random order",
    ),
    (
        "bytes",
        "memory",
        "ln_bytes",
        "B/row",
        "resident growth over build + groupings, variables' heap excluded",
    ),
]
COMPARE = [("MD", "map", "map / dreamor"), ("FD", "flat", "flat / dreamor")]

BENCH_NUMERIC = [
    "N_target",
    "N",
    "rep",
    "seed",
    "k1_count",
    "build_ms",
    "prep_ms",
    "g1_ms",
    "g2_ms",
    "g3_ms",
    "traverse_ms",
    "hit_ns",
    "miss_ns",
    "container_bytes",
    "peak_rss_bytes",
    "groups",
    "lookups",
    "check_groups",
    "check_traverse",
    "sink",
    "order",
    "wall_s",
]

BLUE = Font(name=FONT, color="0000FF")
BOLD = Font(name=FONT, bold=True)
TITLE = Font(name=FONT, bold=True, size=14)
HEAD_FILL = PatternFill("solid", fgColor="D9E1F2")
INPUT_FILL = PatternFill("solid", fgColor="FFFF00")


def header(ws, row, values, col=1):
    for i, v in enumerate(values):
        c = ws.cell(row=row, column=col + i, value=v)
        c.font = BOLD
        c.fill = HEAD_FILL
        c.alignment = Alignment(wrap_text=True, vertical="center")


def finish_fonts(wb):
    for ws in wb.worksheets:
        for row in ws.iter_rows():
            for c in row:
                if c.font is None or c.font.name != FONT:
                    f = c.font
                    c.font = Font(
                        name=FONT,
                        bold=f.bold,
                        italic=f.italic,
                        size=f.size,
                        color=f.color,
                    )


def cell_id(levels):
    return "|".join(levels)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    with open(src) as f:
        rows = list(csv.DictReader(f))
    meta_path = os.path.splitext(src)[0] + "_meta.json"
    meta = json.load(open(meta_path)) if os.path.exists(meta_path) else {}

    # Only the arms and cells that actually ran (--impls)
    global IMPLS, COMPARE
    present = {r["impl"] for r in rows if r["status"] == "ok"}
    IMPLS = [c for c in IMPLS if c in present]
    COMPARE = [cmp for cmp in COMPARE if cmp[1] in present]
    run_cells = {tuple(r[f[0]] for f in FACTORS) for r in rows if r["phase"] == "factorial" and r["status"] == "ok"}

    wb = Workbook()
    ws_sum = wb.active
    ws_sum.title = "Summary"
    ws_design = wb.create_sheet("Design")
    ws_cell = wb.create_sheet("Cells")
    ws_eff = wb.create_sheet("Effects")
    ws_ext = wb.create_sheet("Scale")
    ws_chk = wb.create_sheet("Check")
    ws_chart = wb.create_sheet("Charts")
    ws_raw = wb.create_sheet("Raw")

    # ------------------------------------------------------------------------------- Raw
    base_cols = list(rows[0].keys()) if rows else []
    header(ws_raw, 1, base_cols)
    col = {name: i + 1 for i, name in enumerate(base_cols)}
    for r, row in enumerate(rows, start=2):
        for name, v in row.items():
            if name in BENCH_NUMERIC and v not in ("", None):
                v = float(v)
                if v.is_integer():
                    v = int(v)
            ws_raw.cell(row=r, column=col[name], value=v)
    last = len(rows) + 1

    def rc(name):  # Raw column letter
        return L(col[name])

    cell_formula = '&"|"&'.join(f"{rc(f[0])}{{r}}" for f in FACTORS)
    derived = [
        ("cell", lambda r: "=" + cell_formula.format(r=r)),
        (
            "model_build_ms",
            lambda r: f'=IF({rc("status")}{r}="ok",{rc("build_ms")}{r}+{rc("prep_ms")}{r},"")',
        ),
        (
            "group_ms",
            lambda r: f'=IF({rc("status")}{r}="ok",{rc("g1_ms")}{r}+{rc("g2_ms")}{r}+{rc("g3_ms")}{r},"")',
        ),
        (
            "build_ns_row",
            lambda r: f'=IFERROR({L(len(base_cols) + 2)}{r}*1000000/{rc("N")}{r},"")',
        ),
        (
            "group_ns_visit",
            lambda r: f'=IFERROR({L(len(base_cols) + 3)}{r}*1000000/({rc("N")}{r}*IF({rc("W")}{r}="mixed",3,1)),"")',
        ),
        (
            "traverse_ns_row",
            lambda r: f'=IFERROR({rc("traverse_ms")}{r}*1000000/{rc("N")}{r},"")',
        ),
        (
            "bytes_row",
            lambda r: f'=IFERROR({rc("container_bytes")}{r}/{rc("N")}{r},"")',
        ),
    ]
    start = len(base_cols) + 1
    for i, (name, _) in enumerate(derived):
        col[name] = start + i
    for name, src_name in [
        ("ln_build", "build_ns_row"),
        ("ln_group", "group_ns_visit"),
        ("ln_hit", "hit_ns"),
        ("ln_bytes", "bytes_row"),
        ("ln_build_ms", "model_build_ms"),
        ("ln_group_ms", "group_ms"),
    ]:
        col[name] = len(col) + 1
        derived.append((name, (lambda s: lambda r: f'=IFERROR(LN({L(col[s])}{r}),"")')(src_name)))
    for name in ["ln_build", "ln_group", "ln_hit", "ln_bytes"]:
        col[name + "_sq"] = len(col) + 1
        derived.append((name + "_sq", (lambda s: lambda r: f'=IFERROR({L(col[s])}{r}^2,"")')(name)))
    header(ws_raw, 1, [d[0] for d in derived], col=start)
    for r in range(2, last + 1):
        for i, (name, f) in enumerate(derived):
            ws_raw.cell(row=r, column=start + i, value=f(r))
    ws_raw.freeze_panes = "B2"

    def rng(name):
        return f"Raw!${rc(name)}$2:${rc(name)}${last}"

    # ------------------------------------------------------------------------------- Cells
    ws = ws_cell
    cells = [c for c in itertools.product(*[(f[2], f[3]) for f in FACTORS]) if c in run_cells]
    ws["A1"] = (
        f"Cells - the {len(cells)} factorial cells (phase=factorial, status=ok). "
        "Geometric mean = EXP(mean of ln over repetitions)."
    )
    ws["A1"].font = TITLE
    nf = len(FACTORS)
    hdr = [f[0] for f in FACTORS] + ["cell"] + [f"c_{f[0]}" for f in FACTORS]
    pairs = list(itertools.combinations([f[0] for f in FACTORS], 2))
    hdr += [f"c_{a}{b}" for a, b in pairs]
    hdr += [f"n_{c}" for c in IMPLS]
    ccol = {name: i + 1 for i, name in enumerate(hdr)}
    # Hidden block: ln mean and variance per response x arm
    for key, *_ in RESPONSES:
        for c in IMPLS:
            for stat in ("m", "v"):
                name = f"{stat}_{key}_{c}"
                ccol[name] = len(ccol) + 1
                hdr.append(name)
    # Visible block per response: geometric means, ratios, ln differences and their variances
    for key, rname, *_ in RESPONSES:
        for c in IMPLS:
            ccol[f"g_{key}_{c}"] = len(ccol) + 1
            hdr.append(f"{rname} {c}")
        for k, _, label in COMPARE:
            ccol[f"r_{key}_{k}"] = len(ccol) + 1
            hdr.append(f"{rname} ratio {label}")
        for k, _, _ in COMPARE:
            ccol[f"d_{key}_{k}"] = len(ccol) + 1
            hdr.append(f"d_{key}_{k}")
            ccol[f"vd_{key}_{k}"] = len(ccol) + 1
            hdr.append(f"vd_{key}_{k}")
    header(ws, 2, hdr)
    R0 = 3
    for i, levels in enumerate(cells):
        r = R0 + i
        for j, v in enumerate(levels):
            ws.cell(row=r, column=j + 1, value=v)
        ws.cell(
            row=r,
            column=ccol["cell"],
            value="=" + '&"|"&'.join(f"{L(j + 1)}{r}" for j in range(nf)),
        )
        for j, f in enumerate(FACTORS):
            ws.cell(
                row=r,
                column=ccol[f"c_{f[0]}"],
                value=f'=IF({L(j + 1)}{r}="{f[3]}",1,-1)',
            )
        for a, b in pairs:
            ws.cell(
                row=r,
                column=ccol[f"c_{a}{b}"],
                value=f"={L(ccol['c_' + a])}{r}*{L(ccol['c_' + b])}{r}",
            )
        crit = lambda c: (
            f'{rng("cell")},${L(ccol["cell"])}{r},{rng("impl")},"{c}",{rng("phase")},"factorial",{rng("status")},"ok"'
        )
        for c in IMPLS:
            ws.cell(row=r, column=ccol[f"n_{c}"], value=f"=COUNTIFS({crit(c)})")
        for key, rname, lncol, *_ in RESPONSES:
            for c in IMPLS:
                n = f"{L(ccol['n_' + c])}{r}"
                m = f"{L(ccol[f'm_{key}_{c}'])}{r}"
                ws.cell(
                    row=r,
                    column=ccol[f"m_{key}_{c}"],
                    value=f'=IFERROR(AVERAGEIFS({rng(lncol)},{crit(c)}),"")',
                )
                ws.cell(
                    row=r,
                    column=ccol[f"v_{key}_{c}"],
                    value=f"=IF({n}>1,(SUMIFS({rng(lncol + '_sq')},{crit(c)})-{n}*{m}^2)/({n}-1),0)",
                )
                ws.cell(row=r, column=ccol[f"g_{key}_{c}"], value=f'=IFERROR(EXP({m}),"")')
            for k, other, _ in COMPARE:
                mo, mb = (
                    f"{L(ccol[f'm_{key}_{other}'])}{r}",
                    f"{L(ccol[f'm_{key}_{BASE}'])}{r}",
                )
                vo, vb = (
                    f"{L(ccol[f'v_{key}_{other}'])}{r}",
                    f"{L(ccol[f'v_{key}_{BASE}'])}{r}",
                )
                no, nb = f"{L(ccol['n_' + other])}{r}", f"{L(ccol['n_' + BASE])}{r}"
                ws.cell(row=r, column=ccol[f"d_{key}_{k}"], value=f'=IFERROR({mo}-{mb},"")')
                ws.cell(
                    row=r,
                    column=ccol[f"r_{key}_{k}"],
                    value=f'=IFERROR(EXP({L(ccol[f"d_{key}_{k}"])}{r}),"")',
                )
                ws.cell(
                    row=r,
                    column=ccol[f"vd_{key}_{k}"],
                    value=f'=IFERROR({vo}/{no}+{vb}/{nb},"")',
                )
    R1 = R0 + len(cells) - 1
    for key, *_ in RESPONSES:
        for c in IMPLS:
            for r in range(R0, R1 + 1):
                ws.cell(row=r, column=ccol[f"g_{key}_{c}"]).number_format = "0.0"
        for k, *_ in COMPARE:
            for r in range(R0, R1 + 1):
                ws.cell(row=r, column=ccol[f"r_{key}_{k}"]).number_format = '0.00"x"'
    for n in [n for n in ccol if n.startswith(("m_", "v_", "d_", "vd_", "c_"))]:
        ws.column_dimensions[L(ccol[n])].hidden = True
    ws.column_dimensions[L(ccol["cell"])].width = 34
    ws.freeze_panes = f"{L(ccol['cell'] + 1)}3"

    def cr(name):  # range in Cells
        return f"Cells!${L(ccol[name])}${R0}:${L(ccol[name])}${R1}"

    # ------------------------------------------------------------------------------- Effects
    ws = ws_eff
    ws["A1"] = "Effects - factor effects on ln(ratio), ratio = other arm / dreamor"
    ws["A1"].font = TITLE
    ws["A2"] = (
        f"Effect = mean of the {len(cells) // 2} high-level cells - mean of the {len(cells) // 2} low-level cells. "
        "'Multiplier' = EXP(effect): how many times the ratio changes when the factor goes to its high level "
        "(mixed / owned / sparse / skewed). Standard errors come from the variance between repetitions "
        "within cells (pure error). |t| above about 2 is larger than the noise."
    )
    ws["A2"].alignment = Alignment(wrap_text=True)
    ws.merge_cells("A2:H2")
    ws.row_dimensions[2].height = 45
    terms = (
        [("mean", None)]
        + [(f[0] + " " + f[1], "c_" + f[0]) for f in FACTORS]
        + [(f"{a}x{b}", f"c_{a}{b}") for a, b in pairs]
    )
    eff_ref = {}
    row = 4
    for key, rname, _, unit, _ in RESPONSES:
        for k, other, label in COMPARE:
            ws.cell(row=row, column=1, value=f"{rname} - {label}").font = BOLD
            row += 1
            header(ws, row, ["term", "effect (ln)", "multiplier", "std. error", "t"])
            row += 1
            d, vd = cr(f"d_{key}_{k}"), cr(f"vd_{key}_{k}")
            for name, code in terms:
                ws.cell(row=row, column=1, value=name)
                if code is None:
                    ws.cell(row=row, column=2, value=f"=AVERAGE({d})")
                    ws.cell(row=row, column=4, value=f"=SQRT(SUM({vd}))/{len(cells)}")
                    eff_ref[(key, k)] = f"Effects!$C${row}"
                else:
                    ws.cell(
                        row=row,
                        column=2,
                        value=f"=SUMPRODUCT({cr(code)},{d})/{len(cells) // 2}",
                    )
                    ws.cell(row=row, column=4, value=f"=SQRT(SUM({vd}))/{len(cells) // 2}")
                ws.cell(row=row, column=3, value=f"=EXP(B{row})").number_format = '0.00"x"'
                ws.cell(row=row, column=5, value=f'=IFERROR(B{row}/D{row},"")').number_format = "0.0"
                ws.cell(row=row, column=2).number_format = "0.000"
                ws.cell(row=row, column=4).number_format = "0.000"
                row += 1
            row += 1
    ws.column_dimensions["A"].width = 34
    for c in "BCDE":
        ws.column_dimensions[c].width = 13

    # ------------------------------------------------------------------------------- Scale
    ws = ws_ext
    ws["A1"] = "Scale - predictions at N* from the scale ladder in two reference cells"
    ws["A1"].font = TITLE
    ws["A3"] = "N* (rows to predict)"
    ws["A3"].font = BOLD
    ws["B3"] = 1_000_000_000
    ws["B3"].font = BLUE
    ws["B3"].fill = INPUT_FILL
    ws["B3"].number_format = "#,##0"
    ws["C3"] = "<- change it and the predictions below follow"
    ws["E3"] = "CPU last-level cache (bytes)"
    ws["E3"].font = BOLD
    ws["G3"] = meta.get("llc_bytes") or 32 * 1024 * 1024
    ws["G3"].font = BLUE
    ws["G3"].number_format = "#,##0"
    ws["G3"].comment = Comment(
        "Read by run_doe.py from /sys/devices/system/cpu; 32 MB assumed if unreadable.",
        "make_report",
    )
    ws["A4"] = (
        "Time is fitted as ln(time) = a + b·ln(N) (b = scaling exponent, 1 = linear); memory as bytes = a + b·N. "
        "The solver side is not included (containers only). Time extrapolation is meaningful only when even "
        "the smallest fitted N is well beyond the last-level cache: rungs that fit in cache make the exponent "
        "look larger than it is. So the time prediction uses the top three rungs (N/4, N/2, N), and "
        "'extrapolation' says whether the smallest of them is at least 4x the cache. The full-ladder slope "
        "is shown for reference (it shows the cache transition)."
    )
    ws["A4"].alignment = Alignment(wrap_text=True)
    ws.merge_cells("A4:N4")
    ws.row_dimensions[4].height = 45
    refs = meta.get("reference_cells") or {
        "dense-prefix": dict(W="prefix", O="shared", D="dense", S="uniform"),
        "sparse-mixed": dict(W="mixed", O="owned", D="sparse", S="uniform"),
    }
    ref_label = {name: f"{name} ({'/'.join(ref[f[0]] for f in FACTORS)})" for name, ref in refs.items()}
    ok_rows = [r for r in rows if r["status"] == "ok"]
    row = 6
    header(
        ws,
        row,
        [
            "reference cell",
            "arm",
            "N_target",
            "runs",
            "N (mean)",
            "build ms (geo)",
            "group sums ms (geo)",
            "memory B (mean)",
            "B/row",
            "ln N",
            "ln build ms",
            "ln group ms",
            "cell",
        ],
    )
    row += 1
    blocks = []
    for ref_name, ref in refs.items():
        cid = cell_id(ref[f[0]] for f in FACTORS)
        for c in IMPLS:
            levels = sorted(
                {int(r["N_target"]) for r in ok_rows if r["impl"] == c and cell_id(r[f[0]] for f in FACTORS) == cid}
            )
            if len(levels) < 2:
                continue
            first = row
            for n_t in levels:
                crit = f'{rng("cell")},$M{row},{rng("impl")},$B{row},{rng("N_target")},$C{row},{rng("status")},"ok"'
                ws.cell(row=row, column=1, value=ref_label.get(ref_name, ref_name))
                ws.cell(row=row, column=2, value=c)
                ws.cell(row=row, column=3, value=n_t).number_format = "#,##0"
                ws.cell(row=row, column=4, value=f"=COUNTIFS({crit})")
                ws.cell(row=row, column=5, value=f"=AVERAGEIFS({rng('N')},{crit})").number_format = "#,##0"
                ws.cell(
                    row=row,
                    column=6,
                    value=f"=EXP(AVERAGEIFS({rng('ln_build_ms')},{crit}))",
                ).number_format = "#,##0.0"
                ws.cell(
                    row=row,
                    column=7,
                    value=f"=EXP(AVERAGEIFS({rng('ln_group_ms')},{crit}))",
                ).number_format = "#,##0.0"
                ws.cell(
                    row=row,
                    column=8,
                    value=f"=AVERAGEIFS({rng('container_bytes')},{crit})",
                ).number_format = "#,##0"
                ws.cell(row=row, column=9, value=f"=H{row}/E{row}").number_format = "0.0"
                ws.cell(row=row, column=10, value=f"=LN(E{row})").number_format = "0.000"
                ws.cell(row=row, column=11, value=f"=LN(F{row})").number_format = "0.000"
                ws.cell(row=row, column=12, value=f"=LN(G{row})").number_format = "0.000"
                ws.cell(row=row, column=13, value=cid)
                row += 1
            blocks.append((ref_name, c, first, row - 1))
    row += 1
    header(
        ws,
        row,
        [
            "reference cell",
            "arm",
            "build b (full)",
            "build R²",
            "build b (top: N/4..N)",
            "build @N* (min)",
            "group sums b (top)",
            "group sums @N* (min)",
            "memory slope (B/row)",
            "memory R²",
            "memory @N* (GB)",
            "top-low memory / cache",
            "extrapolation",
        ],
    )
    row += 1
    fit_rows = {}
    for ref_name, c, a, b in blocks:
        lnN, lnB = f"$J${a}:$J${b}", f"$K${a}:$K${b}"
        Nr, Br = f"$E${a}:$E${b}", f"$H${a}:$H${b}"
        p = max(a, b - 2)  # top three rungs (N/4, N/2, N)
        tN, tB, tG = f"$J${p}:$J${b}", f"$K${p}:$K${b}", f"$L${p}:$L${b}"
        ws.cell(row=row, column=1, value=ref_label.get(ref_name, ref_name))
        ws.cell(row=row, column=2, value=c)
        ws.cell(row=row, column=3, value=f"=SLOPE({lnB},{lnN})").number_format = "0.000"
        ws.cell(row=row, column=4, value=f"=RSQ({lnB},{lnN})").number_format = "0.000"
        ws.cell(row=row, column=5, value=f"=SLOPE({tB},{tN})").number_format = "0.000"
        ws.cell(row=row, column=6, value=f"=EXP(INTERCEPT({tB},{tN})+E{row}*LN($B$3))/60000").number_format = "#,##0.0"
        ws.cell(row=row, column=7, value=f"=SLOPE({tG},{tN})").number_format = "0.000"
        ws.cell(row=row, column=8, value=f"=EXP(INTERCEPT({tG},{tN})+G{row}*LN($B$3))/60000").number_format = "#,##0.0"
        ws.cell(row=row, column=9, value=f"=SLOPE({Br},{Nr})").number_format = "0.0"
        ws.cell(row=row, column=10, value=f"=RSQ({Br},{Nr})").number_format = "0.000"
        ws.cell(row=row, column=11, value=f"=(INTERCEPT({Br},{Nr})+I{row}*$B$3)/1073741824").number_format = "#,##0.0"
        ws.cell(row=row, column=12, value=f"=H{p}/$G$3").number_format = "0.0"
        ws.cell(
            row=row,
            column=13,
            value=f'=IF(L{row}>=4,"valid","includes cache-sized rungs - time may be overestimated")',
        )
        fit_rows[(ref_name, c)] = row
        row += 1
    for c, w in zip("ABCDEFGHIJKLM", [40, 11, 13, 9, 14, 13, 13, 15, 14, 10, 13, 14, 40]):
        ws.column_dimensions[c].width = w

    # ------------------------------------------------------------------------------- Check
    ws = ws_chk
    ws["A1"] = "Check - did every arm hand out the same variables in the same groups, per model (cell, N, repetition)?"
    ws["A1"].font = TITLE
    ws["A2"] = (
        "groups = non-empty groups summed. check_groups = sum over groups of (the group's tag sum x a hash of "
        "the grouping and key). check_traverse = tag sum over a full traversal. Tags come from each row's key, "
        "not from addresses, so equal values across arms mean the same variables in the same groups."
    )
    ws["A2"].alignment = Alignment(wrap_text=True)
    ws.merge_cells("A2:K2")
    ws.row_dimensions[2].height = 45
    header(
        ws,
        4,
        [
            "cell",
            "N_target",
            "rep",
            "groups max",
            "groups min",
            "check_groups max",
            "check_groups min",
            "check_traverse max",
            "check_traverse min",
            "arms",
            "result",
        ],
    )
    keys = sorted({(cell_id(r[f[0]] for f in FACTORS), int(r["N_target"]), int(r["rep"])) for r in ok_rows})
    row = 5
    for cid, n_t, rep in keys:
        crit = f'{rng("cell")},$A{row},{rng("N_target")},$B{row},{rng("rep")},$C{row},{rng("status")},"ok"'
        ws.cell(row=row, column=1, value=cid)
        ws.cell(row=row, column=2, value=n_t)
        ws.cell(row=row, column=3, value=rep)
        for j, name in enumerate(["groups", "check_groups", "check_traverse"]):
            ws.cell(row=row, column=4 + 2 * j, value=f"=_xlfn.MAXIFS({rng(name)},{crit})")
            ws.cell(row=row, column=5 + 2 * j, value=f"=_xlfn.MINIFS({rng(name)},{crit})")
        ws.cell(row=row, column=10, value=f"=COUNTIFS({crit})")
        ws.cell(
            row=row,
            column=11,
            value=f'=IF(AND(D{row}=E{row},F{row}=G{row},H{row}=I{row}),"match","MISMATCH")',
        )
        for c in range(4, 10):
            ws.cell(row=row, column=c).number_format = "#,##0"
        row += 1
    chk_last = row - 1
    ws.column_dimensions["A"].width = 32
    for c in "DEFGHI":
        ws.column_dimensions[c].width = 22

    # ------------------------------------------------------------------------------- Design
    ws = ws_design
    ws["A1"] = "Design"
    ws["A1"].font = TITLE
    r = 3
    ws.cell(row=r, column=1, value="Model").font = BOLD
    r += 1
    for line in [
        "A 3-index set (K1, K2, K3). Each K1 has a K2 list C2(k1) and a K3 list C3(k1). "
        "Rows = {(k1,k2,k3): k2 in C2(k1), k3 in C3(k1), keep}. One variable per row.",
        f"On average {meta.get('f2', 50)} K2s and {meta.get('f3', 100)} K3s per K1. K1s are added until there are N "
        "rows, so every cell has the same row count.",
        "Same repetition number = same seed = same model: the arms are measured on the same rows, pairwise.",
    ]:
        ws.cell(row=r, column=1, value=line)
        r += 1
    r += 1
    header(
        ws,
        r,
        ["factor", "name", "low (-1)", "high (+1)", "what the high level exercises"],
    )
    r += 1
    why = {
        "W": "groupings by keys other than the first: the K2 and K3 sums",
        "O": "K2s that belong to a single K1: many small K2 groups instead of few large ones",
        "D": "a sparse set: 10% of candidates kept, predicate evaluated, 10x the K1s",
        "S": "Pareto-distributed list sizes with the same mean: uneven group sizes",
    }
    for f in FACTORS:
        for i, v in enumerate([f[0], f[1], f[2], f[3], why[f[0]]]):
            ws.cell(row=r, column=1 + i, value=v)
        r += 1
    r += 1
    header(ws, r, ["arm", "implementation", "group sums"])
    r += 1
    for c, impl, path in [
        (
            "dreamor",
            "makeSet(...).where(keep).collect(), one VarTable, groupBy per grouping",
            "sumOf(env, rows, vX).build() per group",
        ),
        (
            "map",
            "std::map<tuple<E1*,E2*,E3*>, IloNumVar> filled by nested loops (as examples/production_planning_plain.hpp); "
            "owner lists per K2 and K3 for the mixed workload",
            "loop over the data again, keep(), map lookup per variable",
        ),
        (
            "flat",
            "rows and variables in parallel vectors, std::unordered_map index on a packed integer key, "
            "CSR (counting sort) per grouping",
            "CSR slice per group, direct indexing",
        ),
    ]:
        if c not in IMPLS:
            continue
        for i, v in enumerate([INAME[c], impl, path]):
            ws.cell(row=r, column=1 + i, value=v)
        r += 1
    r += 1
    header(ws, r, ["response", "unit", "definition"])
    r += 1
    for key, rname, _, unit, desc in RESPONSES:
        for i, v in enumerate([rname, unit, desc]):
            ws.cell(row=r, column=1 + i, value=v)
        r += 1
    r += 1
    ws.cell(row=r, column=1, value="Fairness").font = BOLD
    r += 1
    for line in [
        "No solver: bench/stub makes each variable a 16-byte heap object behind a handle and each expression a vector "
        "of variable pointers. Every arm creates the same variables the same way and builds the same expressions.",
        "Every group expression is consumed the same way: its variables are read once and it is ended.",
        "Runs in a seeded random order, each in a fresh process.",
        "Memory is resident growth over build + groupings, minus the variables' own heap (measured at start-up).",
        "Check sheet: the arms handed out the same variables in the same groups.",
    ]:
        ws.cell(row=r, column=1, value=line)
        r += 1
    r += 1
    ws.cell(row=r, column=1, value="Machine (recorded by run_doe.py)").font = BOLD
    r += 1
    for k in [
        "design",
        "impls",
        "N",
        "budget_gb",
        "reps",
        "seed",
        "cpu",
        "mem_total_bytes",
        "llc_bytes",
        "kernel",
        "compiler",
        "python",
        "started",
    ]:
        ws.cell(row=r, column=1, value=k)
        v = meta.get(k, "")
        if isinstance(v, (list, dict)):
            v = ", ".join(v) if isinstance(v, list) else json.dumps(v)
        c = ws.cell(row=r, column=2, value=v)
        if k in ("N", "mem_total_bytes", "llc_bytes"):
            c.number_format = "#,##0"
        r += 1
    ws.column_dimensions["A"].width = 30
    ws.column_dimensions["B"].width = 70
    ws.column_dimensions["C"].width = 40
    ws.column_dimensions["D"].width = 18
    ws.column_dimensions["E"].width = 70

    # ------------------------------------------------------------------------------- Summary
    ws = ws_sum
    ws["A1"] = "Container benchmark - dreamor vs " + " vs ".join(INAME[c] for c in IMPLS if c != BASE)
    ws["A1"].font = TITLE
    ws["A3"] = "rows N"
    ws["B3"] = meta.get("N", "")
    ws["B3"].number_format = "#,##0"
    ws["A4"] = "successful runs"
    ws["B4"] = f'=COUNTIFS({rng("status")},"ok")'
    ws["A5"] = "failed or timed out"
    ws["B5"] = f'=COUNTIFS({rng("status")},"<>ok")'
    ws["A6"] = "check mismatches"
    ws["B6"] = f'=COUNTIFS(Check!$K$5:$K${max(chk_last, 5)},"MISMATCH")'
    ws["C6"] = "must be 0"
    r = 8
    ws.cell(
        row=r,
        column=1,
        value=f"Geometric-mean ratio over all {len(cells)} cells "
        "(above 1: dreamor is that many times faster or smaller)",
    ).font = BOLD
    r += 1
    header(
        ws,
        r,
        ["response", "unit"] + [x for _, _, label in COMPARE for x in (label, "min cell", "max cell")],
    )
    r += 1
    for key, rname, _, unit, _ in RESPONSES:
        ws.cell(row=r, column=1, value=rname)
        ws.cell(row=r, column=2, value=unit)
        for j, (k, _, _) in enumerate(COMPARE):
            base = 3 + j * 3
            ws.cell(row=r, column=base, value=f"={eff_ref[(key, k)]}")
            ws.cell(row=r, column=base + 1, value=f"=MIN({cr(f'r_{key}_{k}')})")
            ws.cell(row=r, column=base + 2, value=f"=MAX({cr(f'r_{key}_{k}')})")
            for c in range(base, base + 3):
                ws.cell(row=r, column=c).number_format = '0.00"x"'
        r += 1
    r += 1
    ws.cell(row=r, column=1, value="Ratios in the reference cells").font = BOLD
    r += 1
    header(ws, r, ["reference cell", "response"] + [label for _, _, label in COMPARE])
    r += 1
    n_full = meta.get("N") or max((int(x["N_target"]) for x in ok_rows), default=0)
    for ref_name, ref in refs.items():
        cid = cell_id(ref[f[0]] for f in FACTORS)
        for key, rname, lncol, *_ in RESPONSES:
            ws.cell(row=r, column=1, value=ref_label.get(ref_name, ref_name))
            ws.cell(row=r, column=2, value=rname)
            for j, (k, other, _) in enumerate(COMPARE):
                crit = lambda c: (
                    f'{rng("cell")},"{cid}",{rng("impl")},"{c}",{rng("N_target")},{n_full},{rng("status")},"ok"'
                )
                ws.cell(
                    row=r,
                    column=3 + j,
                    value=f'=IFERROR(EXP(AVERAGEIFS({rng(lncol)},{crit(other)})-AVERAGEIFS({rng(lncol)},{crit(BASE)})),"none")',
                ).number_format = '0.00"x"'
            r += 1
    r += 1
    ws.cell(row=r, column=1, value="Prediction at N* (containers only, Scale sheet)").font = BOLD
    ws.cell(row=r, column=3, value="=Scale!$B$3").number_format = '#,##0" rows"'
    r += 1
    header(
        ws,
        r,
        [
            "reference cell",
            "arm",
            "memory (GB)",
            "build (min)",
            "build exponent b (top)",
            "extrapolation",
        ],
    )
    r += 1
    for (ref_name, c), fr in fit_rows.items():
        ws.cell(row=r, column=1, value=ref_label.get(ref_name, ref_name))
        ws.cell(row=r, column=2, value=INAME[c])
        ws.cell(row=r, column=3, value=f"=Scale!$K${fr}").number_format = "#,##0.0"
        ws.cell(row=r, column=4, value=f"=Scale!$F${fr}").number_format = "#,##0.0"
        ws.cell(row=r, column=5, value=f"=Scale!$E${fr}").number_format = "0.000"
        ws.cell(row=r, column=6, value=f"=Scale!$M${fr}")
        r += 1
    ws.column_dimensions["A"].width = 44
    for c in "BCDEFGH":
        ws.column_dimensions[c].width = 17

    # ------------------------------------------------------------------------------- Charts
    ws = ws_chart
    ws["A1"] = "Charts (values from the Cells and Scale sheets)"
    ws["A1"].font = TITLE
    for idx, (key, rname, _, unit, _) in enumerate(RESPONSES):
        ch = BarChart()
        ch.type = "bar"
        ch.title = f"{rname}: ratio per cell (other arm / dreamor)"
        ch.y_axis.title = "ratio"
        ch.height, ch.width = 14, 22
        for k, _, label in COMPARE:
            ch.add_data(
                Reference(ws_cell, min_col=ccol[f"r_{key}_{k}"], min_row=2, max_row=R1),
                titles_from_data=True,
            )
        ch.set_categories(Reference(ws_cell, min_col=ccol["cell"], min_row=R0, max_row=R1))
        ws.add_chart(ch, f"A{3 + idx * 30}")
    sc = ScatterChart()
    sc.title = "Scale ladder: model build time (log-log)"
    sc.x_axis.title = "N (rows)"
    sc.y_axis.title = "build ms"
    sc.x_axis.scaling.logBase = 10
    sc.y_axis.scaling.logBase = 10
    sc.height, sc.width = 14, 22
    for ref_name, c, a, b in blocks:
        s = Series(
            Reference(ws_ext, min_col=6, min_row=a, max_row=b),
            Reference(ws_ext, min_col=5, min_row=a, max_row=b),
            title=f"{ref_name}-{c}",
        )
        s.marker.symbol = "circle"
        sc.series.append(s)
    ws.add_chart(sc, "P3")

    finish_fonts(wb)

    out_dir = os.path.dirname(out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    wb.save(out)
    print(f"saved: {out}  (Raw {len(rows)} rows)")


if __name__ == "__main__":
    main()

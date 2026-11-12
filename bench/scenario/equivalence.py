#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that every arm of a scenario hands each solver the same model, before a run measures the arms.

A run's own checks count what the solver was given: its rows, columns and nonzeros. This script compares what the
solver was given. At each scenario's smallest size, every arm on each solver writes the model that the solver holds.
An arm writes the model if SCENARIO_EXPORT is set, outside every timer. ../common/compare_mps.py compares each model
with dreamor's by meaning: the bounds, integrality, costs, right-hand sides and coefficients must agree within a
relative 1e-12. The names that a solver makes up for what a model left unnamed are compared by what they hold.
MathOpt writes six significant digits, so on MathOpt the arms are compared to six digits.

    bench/scenario/build.sh && python3 bench/scenario/equivalence.py

If an arm's model differs from dreamor's on some solver, the script exits with status 1.
"""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
COMPARE = os.path.join(os.path.dirname(HERE), "common", "compare_mps.py")
# The names that the solvers make up: CPLEX's x12, c34 and i5, HiGHS's c0 and r1, and MathOpt's numbered ids.
MADE_UP = r"[A-Za-z_]?[0-9]+"

SCENARIOS = {
    "pmedian": (["--L", "5000"], ["dreamor", "hand", "calls", "callable", "callable-rows"]),
    "lotsizing": (["--items", "170", "--seed", "7"], ["dreamor", "flat", "plain", "callable", "callable-rows"]),
}
SOLVERS = ["cplex", "mathopt", "highs"]


def export(binary, impl, size, path):
    """Returns the path of the model that the arm handed the solver, or None if the arm does not run on this solver."""
    env = dict(os.environ, SCENARIO_EXPORT=path)
    out = subprocess.run([binary, "--impl", impl, *size], env=env, capture_output=True, text=True, check=True).stdout
    run = json.loads(out.strip().splitlines()[-1])
    return None if run.get("skipped") else path + ".mps"


def main():
    differ = 0
    with tempfile.TemporaryDirectory() as tmp:
        for scenario, (size, impls) in SCENARIOS.items():
            for solver in SOLVERS:
                # The dreamor arm runs on build/<scenario>_<solver>, and the hand-written arms run on
                # build/<scenario>_<solver>_hand.
                binary = os.path.join(HERE, "build", f"{scenario}_{solver}")
                if not (os.path.isfile(binary) and os.path.isfile(binary + "_hand")):
                    print(f"{scenario} {solver}: not built, skipped")
                    continue
                models = {
                    impl: export(
                        binary if impl == "dreamor" else binary + "_hand",
                        impl,
                        size,
                        os.path.join(tmp, f"{scenario}_{solver}_{impl}"),
                    )
                    for impl in impls
                }
                for impl, model in models.items():
                    if impl == "dreamor" or model is None:
                        continue
                    compared = subprocess.run(
                        [sys.executable, COMPARE, "--made-up", MADE_UP, models["dreamor"], model],
                        capture_output=True,
                        text=True,
                    )
                    # If the models agree, the last two lines of the output say how many made-up columns, rows and
                    # terms were compared by what they hold, and give the counts of the named part.
                    last = (
                        "; ".join(compared.stdout.strip().splitlines()[-2:])
                        if compared.stdout.strip()
                        else compared.stderr.strip()
                    )
                    verdict = "same model" if compared.returncode == 0 else "DIFFERS"
                    differ += compared.returncode != 0
                    print(f"{scenario} {solver}: {impl} against dreamor - {verdict} ({last})")
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())

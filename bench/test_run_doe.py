#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests run_doe.py: the CPU choice on fake /sys and /proc trees, and the design. python3 -m unittest test_run_doe"""

import itertools
import os
import tempfile
import unittest

import run_doe


def write(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text + "\n")


def machine(root, cpus, pcores=None, release="6.8.0-generic", flags="fpu sse2 avx2"):
    """cpus: cpu -> dict(l3='32M', perf=int|None, freq=int|None, core='0,8')."""
    write(root, "proc/sys/kernel/osrelease", release)
    write(root, "proc/cpuinfo", f"processor\t: 0\nflags\t\t: {flags}")
    write(root, "sys/devices/system/cpu/online", ",".join(str(c) for c in sorted(cpus)))
    if pcores is not None:
        write(root, "sys/devices/cpu_core/cpus", pcores)
    for cpu, spec in cpus.items():
        base = f"sys/devices/system/cpu/cpu{cpu}"
        write(root, f"{base}/cache/index0/level", "1")
        write(root, f"{base}/cache/index0/size", "48K")
        write(root, f"{base}/cache/index3/level", "3")
        write(root, f"{base}/cache/index3/size", spec.get("l3", "32M"))
        write(root, f"{base}/topology/core_cpus_list", spec.get("core", str(cpu)))
        if spec.get("perf") is not None:
            write(root, f"{base}/acpi_cppc/highest_perf", str(spec["perf"]))
        if spec.get("freq") is not None:
            write(root, f"{base}/cpufreq/cpuinfo_max_freq", str(spec["freq"]))


class ChooseCpu(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_intel_hybrid_takes_a_pcore_not_cpu0(self):
        # 4 P-cores with SMT (0/1, 2/3, 4/5, 6/7), 4 E-cores (8..11); E-cores clock lower.
        cpus = {c: dict(core=f"{c - c % 2}-{c - c % 2 + 1}", freq=5_400_000) for c in range(8)}
        cpus.update({c: dict(freq=3_900_000) for c in range(8, 12)})
        machine(self.root, cpus, pcores="0-7")
        cpu, info = run_doe.choose_cpu(self.root)
        self.assertEqual(cpu, 2)
        self.assertTrue(info["hybrid"])
        self.assertIn("P-cores", info["reason"])

    def test_preferred_core_by_cppc(self):
        # Homogeneous 8 cores (a 5700X): CPPC ranks cores 3 and 5 highest.
        cpus = {c: dict(perf=166 if c in (3, 5) else 150, freq=4_650_000) for c in range(8)}
        machine(self.root, cpus)
        cpu, info = run_doe.choose_cpu(self.root)
        self.assertEqual(cpu, 3)
        self.assertIn("CPPC", info["reason"])
        self.assertFalse(info["hybrid"])

    def test_x3d_takes_the_vcache_ccd(self):
        # CCD0 (0..7) has the 96 MB V-cache, CCD1 (8..15) 32 MB and the higher clocks.
        cpus = {c: dict(l3="96M", perf=150) for c in range(8)}
        cpus.update({c: dict(l3="32M", perf=170) for c in range(8, 16)})
        machine(self.root, cpus)
        cpu, info = run_doe.choose_cpu(self.root)
        self.assertEqual(cpu, 1)
        self.assertIn("96 MB", info["reason"])

    def test_cpu0_only_when_alone(self):
        machine(self.root, {0: dict(freq=1)})
        cpu, _ = run_doe.choose_cpu(self.root)
        self.assertEqual(cpu, 0)

    def test_wsl_is_not_pinned(self):
        machine(self.root, {c: {} for c in range(4)}, release="5.15.167.4-microsoft-standard-WSL2")
        cpu, info = run_doe.choose_cpu(self.root)
        self.assertIsNone(cpu)
        self.assertEqual(info["virtualized"], "WSL")

    def test_vm_is_not_pinned(self):
        machine(self.root, {c: {} for c in range(4)}, flags="fpu sse2 hypervisor avx2")
        cpu, info = run_doe.choose_cpu(self.root)
        self.assertIsNone(cpu)
        self.assertIn("hypervisor", info["virtualized"])

    def test_last_level_cache_is_per_cpu(self):
        cpus = {0: dict(l3="96M"), 1: dict(l3="32M")}
        machine(self.root, cpus)
        self.assertEqual(run_doe.last_level_cache(self.root, 0), 96 * 1024**2)
        self.assertEqual(run_doe.last_level_cache(self.root, 1), 32 * 1024**2)

    def test_parse_cpulist(self):
        self.assertEqual(run_doe.parse_cpulist("0-3,8,10-11"), [0, 1, 2, 3, 8, 10, 11])
        self.assertEqual(run_doe.parse_cpulist(""), [])


class Design(unittest.TestCase):
    def test_full_has_every_cell(self):
        self.assertEqual(len(run_doe.design_cells(full=True)), 288)

    def test_half_fraction_is_resolution_five(self):
        cells = run_doe.design_cells(full=False)
        self.assertEqual(len(cells), 144)
        two = [f for f, lv in run_doe.FACTORS.items() if len(lv) == 2]

        def sign(cell, f):
            return 1 if cell[f] == run_doe.FACTORS[f][1] else -1

        # Main effects and two-factor interactions of the 2-level factors: balanced, and every
        # pair of them orthogonal - nothing up to two factors is aliased with anything else.
        columns = {(f,): [sign(c, f) for c in cells] for f in two}
        for a, b in itertools.combinations(two, 2):
            columns[(a, b)] = [sign(c, a) * sign(c, b) for c in cells]
        for name, col in columns.items():
            self.assertEqual(sum(col), 0, name)
        for (n1, c1), (n2, c2) in itertools.combinations(columns.items(), 2):
            self.assertEqual(sum(x * y for x, y in zip(c1, c2)), 0, (n1, n2))
        # N and A crossed in full with every 2-level combination.
        self.assertEqual(len({(c["N_target"], c["A"]) for c in cells}), 9)


if __name__ == "__main__":
    unittest.main()

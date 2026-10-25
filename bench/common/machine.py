#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The machine and the code a run measures, for every runner under bench/: which CPU to pin, what the
machine is set to, and which commit (and uncommitted diff) the binaries were built from."""

import hashlib
import os
import platform
import re
import subprocess


def read_text(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def read_int(path):
    text = read_text(path)
    try:
        return int(text) if text is not None else None
    except ValueError:
        return None


def parse_cpulist(text):
    """'0-3,8,10-11' -> [0, 1, 2, 3, 8, 10, 11]."""
    cpus = []
    for part in (text or "").split(","):
        part = part.strip()
        if not part:
            continue
        lo, _, hi = part.partition("-")
        cpus.extend(range(int(lo), int(hi or lo) + 1))
    return cpus


def virtualization(root="/"):
    """Why pinning would not pin a physical core here, or None on bare metal."""
    release = read_text(os.path.join(root, "proc/sys/kernel/osrelease")) or ""
    if "microsoft" in release.lower():
        return "WSL"
    for line in (read_text(os.path.join(root, "proc/cpuinfo")) or "").splitlines():
        if line.startswith("flags") and "hypervisor" in line.split(":", 1)[-1].split():
            return "virtual machine (hypervisor flag)"
    return None


def cpu_dir(root, cpu):
    return os.path.join(root, "sys/devices/system/cpu", f"cpu{cpu}")


def online_cpus(root="/"):
    base = os.path.join(root, "sys/devices/system/cpu")
    cpus = parse_cpulist(read_text(os.path.join(base, "online")))
    if cpus:
        return cpus
    try:
        return sorted(int(d[3:]) for d in os.listdir(base) if re.fullmatch(r"cpu\d+", d))
    except OSError:
        return list(range(os.cpu_count() or 1))


def last_level_cache(root="/", cpu=0):
    """Size in bytes of the last-level cache that CPU sees (its CCD's L3 on a split part)."""
    best_level, best_size = 0, 0
    base = os.path.join(cpu_dir(root, cpu), "cache")
    try:
        entries = os.listdir(base)
    except OSError:
        return None
    for d in entries:
        if not d.startswith("index"):
            continue
        level = read_int(os.path.join(base, d, "level"))
        text = read_text(os.path.join(base, d, "size"))
        if level is None or not text:
            continue
        mult = {"K": 1024, "M": 1024**2, "G": 1024**3}.get(text[-1], 1)
        size = int(text.rstrip("KMG")) * mult
        if level > best_level or (level == best_level and size > best_size):
            best_level, best_size = level, size
    return best_size or None


def choose_cpu(root="/"):
    """The CPU --cpu auto pins to, or None, and how it was chosen.

    Core type is noise, not a factor, so a run stays on one core class:
      1. Intel hybrid: the P-cores (/sys/devices/cpu_core/cpus);
      2. the largest last-level cache among those (the V-cache CCD of a dual-CCD X3D part);
      3. the highest ACPI CPPC highest_perf, else the highest cpuinfo_max_freq (preferred cores);
      4. the lowest-numbered physical core that is not CPU 0's (CPU 0 takes the interrupts).
    A virtualized machine gets None: pinning there pins a virtual CPU only.
    """
    info = {"virtualized": virtualization(root)}
    pcores = read_text(os.path.join(root, "sys/devices/cpu_core/cpus"))
    info["hybrid"] = pcores is not None
    if info["virtualized"]:
        info["reason"] = f"not pinned: {info['virtualized']} pins a virtual CPU only"
        return None, info
    online = online_cpus(root)
    candidates = [c for c in online if c in set(parse_cpulist(pcores))] if pcores else list(online)
    reasons = ["P-cores (Intel hybrid)" if pcores else "one core type"]

    l3 = {c: last_level_cache(root, c) or 0 for c in candidates}
    if len(set(l3.values())) > 1:
        top = max(l3.values())
        candidates = [c for c in candidates if l3[c] == top]
        reasons.append(f"largest last-level cache ({top >> 20} MB)")
    for name, rel in [
        ("CPPC highest_perf", "acpi_cppc/highest_perf"),
        ("max frequency", "cpufreq/cpuinfo_max_freq"),
    ]:
        perf = {c: read_int(os.path.join(cpu_dir(root, c), rel)) for c in candidates}
        if all(v is not None for v in perf.values()):
            if len(set(perf.values())) > 1:
                top = max(perf.values())
                candidates = [c for c in candidates if perf[c] == top]
                reasons.append(f"highest {name}")
            break  # the first metric the machine has decides

    def core_of(c):
        topo = os.path.join(cpu_dir(root, c), "topology")
        text = read_text(os.path.join(topo, "core_cpus_list")) or read_text(os.path.join(topo, "thread_siblings_list"))
        return tuple(parse_cpulist(text)) or (c,)

    cores = sorted({core_of(c) for c in candidates})
    core = next((k for k in cores if 0 not in k), cores[0])
    cpu = min(c for c in candidates if c in core)
    reasons.append(f"physical core {list(core)}" + (" (not CPU 0's)" if 0 not in core else ""))
    info["reason"] = "; ".join(reasons)
    return cpu, info


def environment(root="/", cpu=None):
    """What the machine was set to, for the meta file: what the bench cannot set itself."""
    c = cpu if cpu is not None else 0
    no_turbo = read_int(os.path.join(root, "sys/devices/system/cpu/intel_pstate/no_turbo"))
    boost = read_int(os.path.join(root, "sys/devices/system/cpu/cpufreq/boost"))
    if boost is None and no_turbo is not None:
        boost = 1 - no_turbo
    return dict(
        governor=read_text(os.path.join(cpu_dir(root, c), "cpufreq/scaling_governor")),
        boost=boost,
        smt_active=read_int(os.path.join(root, "sys/devices/system/cpu/smt/active")),
        thp=read_text(os.path.join(root, "sys/kernel/mm/transparent_hugepage/enabled")),
        llc_bytes=last_level_cache(root, c),
        glibc_tunables=os.environ.get("GLIBC_TUNABLES"),
    )


def meminfo():
    info = {}
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                k, v = line.split(":", 1)
                info[k] = int(v.split()[0]) * 1024
    except OSError:
        pass
    return info


def cpu_model():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def compiler_version(cxx):
    try:
        return subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    except Exception:
        return "unknown"


def code_state():
    """(commit, dirty, label) of the repository the benchmark builds from, or Nones without git.

    The label names the code exactly: the short commit, plus a hash of the diff when tracked files
    have uncommitted changes, so two different uncommitted states never share a results folder.
    """
    root = os.path.dirname(os.path.abspath(__file__))

    def git(*cmd):
        return subprocess.run(["git", "-C", root, *cmd], capture_output=True, check=True).stdout

    try:
        commit = git("rev-parse", "HEAD").decode().strip()
        diff = git("diff", "HEAD")
    except (OSError, subprocess.CalledProcessError):
        return None, None, "nogit"
    label = commit[:7]
    if diff:
        label += "-dirty-" + hashlib.sha1(diff).hexdigest()[:6]
    return commit, bool(diff), label


def short_n(n):
    """10000000 -> '1e7', 30000 -> '3e4'."""
    e = len(str(n)) - 1
    return f"{n // 10**e}e{e}"

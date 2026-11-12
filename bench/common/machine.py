#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The machine and the code that a run measures, for every runner under bench/: which CPU to pin, what the machine is
set to, and which commit and uncommitted diff the binaries were built from."""

import ctypes
import ctypes.util
import hashlib
import os
import platform
import re
import struct
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
    """Returns why pinning would not pin a physical core on this machine, or None on bare metal."""
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
    """Returns the size in bytes of the last-level cache that `cpu` sees, or None if the machine does not say.

    On a part with several CCDs, the last-level cache is the L3 of the CCD that holds `cpu`.
    """
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
    """Returns the CPU that --cpu auto pins to, or None, and how it was chosen.

    The core type is noise, not a factor, so a run stays on one class of core. The CPU is chosen in four steps:

      1. On an Intel hybrid part, the candidates are the P-cores that /sys/devices/cpu_core/cpus lists.
      2. If the candidates' last-level caches differ, the candidates with the largest one are kept. On a dual-CCD
         X3D part, they are the CCD with the V-cache.
      3. The preferred cores are kept: the highest ACPI CPPC highest_perf, or the highest cpuinfo_max_freq if the
         candidates do not report highest_perf.
      4. The CPU is the lowest-numbered candidate of the first physical core that does not hold CPU 0, because CPU 0
         takes the interrupts. CPU 0's core is chosen only if no other core is left.

    On a virtualized machine, the function returns None, because pinning there pins a virtual CPU only.
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
            break  # The first metric that every candidate reports decides.

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
    """Returns what the machine was set to, for the meta file: the settings that the benchmark cannot set itself."""
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


PERF_EVENT_OPEN = {"x86_64": 298, "aarch64": 241}  # The number of the perf_event_open syscall on each machine.


def counts_instructions():
    """Returns (True, None) if this process can count its own user-mode instructions, and (False, the reason) if not.

    The function opens the counter that the runs would open, and closes it again, because a virtual machine or a
    container may lack it. The counter is PERF_COUNT_HW_INSTRUCTIONS from perf_event_open, with the kernel excluded,
    as perf_event_paranoid 2 requires.
    """
    nr = PERF_EVENT_OPEN.get(platform.machine())
    if nr is None or platform.system() != "Linux":
        return False, f"no perf_event_open on {platform.system()} {platform.machine()}"
    # The fields of perf_event_attr come in this order: type, size, config, sample_period, sample_type, read_format and
    # the flag bits. The flags set are disabled, bit 0, exclude_kernel, bit 5, and exclude_hv, bit 6. The rest of the
    # 128 bytes stays 0.
    attr = struct.pack("=IIQQQQQ", 0, 128, 1, 0, 0, 0, 1 | 1 << 5 | 1 << 6).ljust(128, b"\0")
    libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
    libc.syscall.restype = ctypes.c_long
    fd = libc.syscall(nr, ctypes.c_char_p(attr), 0, -1, -1, 0)
    if fd < 0:
        return False, f"perf_event_open: {os.strerror(ctypes.get_errno())}"
    os.close(fd)
    return True, None


def choose_pmu(spec):
    """Returns the --pmu value that a run passes on, and why.

    With auto, a run counts instructions if the machine can.
    """
    if spec == "auto":
        ok, why = counts_instructions()
        return ("instructions", "auto: user-mode instructions") if ok else ("", f"auto: none ({why})")
    if spec in ("", "none"):
        return "", "none (--pmu none)"
    return spec, f"given (--pmu {spec})"


def compiler_version(cxx):
    try:
        return subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    except Exception:
        return "unknown"


def code_state():
    """Returns (commit, dirty, label) of the repository that the benchmark builds from.

    Without git, the function returns (None, None, "nogit"). The label names the code exactly: the short commit, and a
    hash of the diff if tracked files have uncommitted changes. So two different uncommitted states never share a
    results folder.
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

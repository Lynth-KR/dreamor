#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Runs what CI runs (.github/workflows/ci.yml) on this machine, over one commit checked out apart: what a push
would publish, not the working tree. It needs every tool CI installs and stops before running anything if one is
missing - a partial run is not a pass.

    ci/local.py               HEAD
    ci/local.py REV           another commit
    ci/local.py --logs DIR    the jobs' logs kept in DIR, passed or not

A pass is recorded by the commit's tree - the content it tested, whatever the message or the signature - in
ci-passed/ under the repository's git directory, where ci/hooks/pre-push looks for it before a push to main.

ORTOOLS_DIR is the OR-Tools C++ prebuilt CI downloads (v9.15; default /opt/ortools). Format and the layer check run
first, with the CMake package installed - seconds, and nothing else runs if one fails; then tidy and the rest run
CI_JOBS at a time (default 4), as CI's runners do, each in a checkout of its own, each building and testing JOBS at
a time (default: the cores shared among CI_JOBS), tidy - the longest - first. The first job that fails stops the run: the jobs running are
stopped, the ones waiting never start. On CI lint runs before the rest, on runners of its own; here tidy, the
lint's long part, runs beside them and stops them all as soon as it fails. Without --logs the logs go to a
temporary folder, kept if a job fails.
The jobs are written down twice: change this together with ci.yml.
"""

import argparse
import hashlib
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GCC_IMAGE = "docker.io/library/gcc:16"
EMSDK_IMAGE = "docker.io/emscripten/emsdk:6.0.13"
# Images this script builds for itself from the commit's ci/containers/, each named by the content of the files it
# is built from, so a change to one of them builds it again: GCC's image with CMake, the newest LLVM's libc++,
# which cannot share a machine with the floor's, and HiGHS built by the wasm job's Emscripten.
IMAGES = {
    "gcc-latest": ["gcc-latest.Containerfile"],
    "libcxx-latest": ["libcxx-latest.Containerfile"],
    "highs-wasm": ["highs-wasm.Containerfile", "build-highs-wasm.sh"],
}


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def missing_tools(ortools: Path) -> list[str]:
    """What CI installs and this machine lacks."""
    lacks = [t for t in ["g++-13", "clang++-18", "clang++-23", "cmake", "podman", "uv"] if not shutil.which(t)]
    if shutil.which("podman"):
        for image in [GCC_IMAGE, EMSDK_IMAGE]:
            if subprocess.run(["podman", "image", "exists", image]).returncode != 0:
                lacks.append(f"the {image} image: podman pull {image}")
    if not (ortools / "include/ortools/math_opt/cpp/math_opt.h").is_file():
        lacks.append(f"OR-Tools at ORTOOLS_DIR ({ortools})")
    if not Path("/usr/lib/llvm-18/include/c++/v1/version").is_file():
        lacks.append("libc++ 18: sudo apt install libc++-18-dev libc++abi-18-dev libunwind-18-dev")
    return lacks


def image_name(rev: str, name: str) -> str:
    blobs = "".join(git("rev-parse", f"{rev}:ci/containers/{f}") + "\n" for f in IMAGES[name])
    return f"localhost/dreamor-{name}:{hashlib.sha256(blobs.encode()).hexdigest()[:12]}"


def build_images(rev: str, work: Path) -> dict[str, str]:
    images = {}
    for name, files in IMAGES.items():
        tag = images[name] = image_name(rev, name)
        if subprocess.run(["podman", "image", "exists", tag]).returncode == 0:
            continue
        context = work / "containers"
        if not context.exists():
            context.mkdir()
            archive = subprocess.run(
                ["git", "archive", rev, "ci/containers"], cwd=ROOT, check=True, capture_output=True
            )
            subprocess.run(["tar", "-x", "-C", str(context)], input=archive.stdout, check=True)
        print(f"ci/local: building {tag} (once per change of what it is built from; minutes)", flush=True)
        folder = context / "ci/containers"
        subprocess.run(
            ["podman", "build", "-q", "-t", tag, "-f", str(folder / files[0]), str(folder)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    return images


def in_image(image: str, command: str) -> str:
    return (
        f'podman run --rm -e CMAKE_BUILD_PARALLEL_LEVEL -e CTEST_PARALLEL_LEVEL -v "$PWD":/src -w /src {image} '
        f"sh -c '{command}'"
    )


# Clang builds on the floor's libstdc++, GCC 13's - Ubuntu 24.04's own - whatever other GCC a machine has, as the
# Clang presets do.
FLOOR_LIBSTDCXX = "--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/13"


def consumer(source: str, build: Path, cxx: str, prefix_path: str, program: str) -> str:
    flags = FLOOR_LIBSTDCXX if cxx.startswith("clang") else ""
    return (
        f"cmake -S {source} -B '{build}' -DCMAKE_CXX_COMPILER={cxx} -DCMAKE_CXX_FLAGS='{flags}' "
        f"-DCMAKE_PREFIX_PATH='{prefix_path}' && "
        f"cmake --build '{build}' && '{build}/{program}'"
    )


def phases(work: Path, images: dict[str, str], ortools: Path) -> list[list[tuple[str, str]]]:
    prefix = work / "prefix"
    workflow = "cmake --workflow --preset"
    # GCC 13 at -O1 -g is sanitize-gcc13; Clang 18 at -O1 -g runs only the compiler's checks, its other tests being
    # sanitize-clang18; the compile-time checks run once per compiler. As on CI.
    rest = [
        (preset, f"{workflow} {preset}")
        for preset in [
            "stub-gcc13-release",
            "stub-clang18-debug",
            "stub-clang18-release",
            "sanitize-gcc13",
            "sanitize-clang18",
            "sanitize-thread-gcc13",
            "clang-latest",
            "libcxx-18",
        ]
    ]
    rest += [
        ("gcc-latest", in_image(images["gcc-latest"], f"{workflow} gcc-latest")),
        ("libcxx-latest", in_image(images["libcxx-latest"], f"{workflow} libcxx-latest")),
        ("wasm", in_image(EMSDK_IMAGE, f"{workflow} wasm")),
        ("wasm-highs", in_image(images["highs-wasm"], f"{workflow} wasm-highs")),
    ]
    for cxx, preset in [("g++-13", "mathopt-gcc13"), ("clang++-18", "mathopt-clang18")]:
        build = work / f"consumer-mathopt-{cxx}"
        rest.append(
            (
                preset,
                f"{workflow} {preset} && "
                + consumer("tests/cmake_consumer_mathopt", build, cxx, f"{prefix};{ortools}", "lot_sizing"),
            )
        )
    for cxx in ["g++-13", "clang++-18"]:
        rest.append(
            (f"package-{cxx}", consumer("tests/cmake_consumer", work / f"consumer-{cxx}", cxx, str(prefix), "consumer"))
        )
    return [
        # Seconds, and nothing else if one fails. The installed package is for the package and mathopt jobs (CI's
        # package is a step of stub-gcc13-release).
        [
            ("format", "tools/format.sh --check"),
            ("layers", "python3 tools/check_layers.py"),
            ("reference", "python3 tools/gen_reference.py --check docs/reference"),
            ("skill-docs", "python3 tools/sync_skill_docs.py --check"),
            ("arity", "python3 tools/gen_arity_tests.py --check"),
            ("bench", "python3 tools/check_bench.py"),
            ("site", "tools/site.sh"),
            (
                "install",
                f"cmake -S . -B '{work / 'build'}' -DCMAKE_INSTALL_PREFIX='{prefix}' && "
                f"cmake --install '{work / 'build'}'",
            ),
        ],
        # tidy first: the longest job, started while the others build.
        [("tidy", "python3 tools/gen_compile_commands.py && tools/tidy.sh"), *rest],
    ]


def run_phase(jobs: list[tuple[str, str]], rev: str, work: Path, logs: Path, ci_jobs: int) -> list[str]:
    """Runs the jobs CI_JOBS at a time, each in a checkout of its own; prints their lines in the order they were
    started, and gives back the names of those that failed. The first to fail stops the rest: each job runs in a
    process group of its own, which is stopped whole - its compilers, its tests, a container's client - and a job
    not started yet is skipped."""
    checkouts = []
    for name, _ in jobs:
        checkout = work / f"src-{name}"
        git("worktree", "add", "-q", "--detach", str(checkout), rev)
        checkouts.append(checkout)
    stop = threading.Event()
    running: dict[str, subprocess.Popen[bytes]] = {}
    lock = threading.Lock()

    def run(job: tuple[str, str], checkout: Path) -> tuple[str, float]:
        start = time.monotonic()
        with lock:
            if stop.is_set():
                return "skipped", 0.0
            with open(logs / f"{job[0]}.log", "w") as log:
                process = running[job[0]] = subprocess.Popen(
                    ["sh", "-c", job[1]],
                    cwd=checkout,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL,
                    start_new_session=True,
                )
        code = process.wait()
        with lock:
            del running[job[0]]
            if code == 0:
                return "ok", time.monotonic() - start
            if stop.is_set():
                return "stopped", time.monotonic() - start
            stop.set()
            for other in running.values():
                os.killpg(other.pid, signal.SIGTERM)
        return "FAILED", time.monotonic() - start

    failed = []
    with ThreadPoolExecutor(ci_jobs) as pool:
        for job, result in zip(jobs, [pool.submit(run, job, c) for job, c in zip(jobs, checkouts)]):
            outcome, took = result.result()
            print(f"  {job[0]:<32} {outcome:<7} {took:4.0f}s", flush=True)
            if outcome == "FAILED":
                failed.append(job[0])
    for checkout in checkouts:
        subprocess.run(["git", "worktree", "remove", "--force", str(checkout)], cwd=ROOT, capture_output=True)
    return failed


def main() -> int:
    parser = argparse.ArgumentParser(description="What CI runs, on this machine, over one commit.")
    parser.add_argument("rev", nargs="?", default="HEAD")
    parser.add_argument("--logs", type=Path, help="keep the jobs' logs here, passed or not")
    args = parser.parse_args()
    rev = git("rev-parse", "--verify", f"{args.rev}^{{commit}}")
    ortools = Path(os.environ.get("ORTOOLS_DIR", "/opt/ortools"))
    lacks = missing_tools(ortools)
    if lacks:
        print("ci/local: this machine lacks what CI has:\n" + "".join(f"  {m}\n" for m in lacks), file=sys.stderr)
        return 1
    # The jobs running at once share the cores: CI_JOBS of them, JOBS processes each, as many as the cores in all. A
    # test's translation unit takes a gigabyte or more to compile, so twice the cores outgrew 32 GB.
    ci_jobs = int(os.environ.get("CI_JOBS", "4"))
    jobs = os.environ.get("JOBS", str(max(1, (os.cpu_count() or 2) // ci_jobs)))
    os.environ.update(ORTOOLS_DIR=str(ortools), JOBS=jobs, CMAKE_BUILD_PARALLEL_LEVEL=jobs, CTEST_PARALLEL_LEVEL=jobs)

    print(f"ci/local: {git('log', '-1', '--format=%h %s', rev)}", flush=True)
    work = Path(tempfile.mkdtemp(prefix="dreamor-ci."))
    logs = args.logs.resolve() if args.logs else work / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    failed = ["stopped before the end"]
    try:
        images = build_images(rev, work)
        for phase in phases(work, images, ortools):
            failed = run_phase(phase, rev, work, logs, ci_jobs)
            if failed:
                break
    finally:
        git("worktree", "prune")
        if failed:
            print(f"ci/local: FAILED: {' '.join(failed)} - logs in {logs}", file=sys.stderr)
        if not failed or args.logs:
            shutil.rmtree(work, ignore_errors=True)
    if failed:
        return 1
    passed = ROOT / git("rev-parse", "--git-common-dir") / "ci-passed"
    passed.mkdir(exist_ok=True)
    short = git("rev-parse", "--short", rev)
    (passed / git("rev-parse", f"{rev}^{{tree}}")).write_text(
        f"{datetime.now():%Y-%m-%d %H:%M:%S} {socket.gethostname()} {short}\n"
    )
    print("ci/local: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

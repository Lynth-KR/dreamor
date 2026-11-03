# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Sourced by the benchmarks' build.sh: compiles run JOBS at a time, and each repetition's code layout.
#
#   spawn <command...>   runs the command in the background, at most $JOBS at once (default: half the cores)
#   finish               waits for every one; exits 1 if any failed
#   make_layouts         writes build/layout/<rep>/ for each repetition (layout.py), before any compile reads it: pad.o,
#                        a padding of the repetition's own size; flags, a response file (no shell expands the
#                        linker's glob) that puts every function in a section of its own and links with lld
#                        ($LLD_DIR, default LLVM 18's) with the sections shuffled by the repetition's seed; and
#                        layout.json, which run.py checks against its --seed. A compile takes @build/layout/<rep>/flags.
#
# LAYOUTS=n builds copies for repetitions 0 .. n-1 (default 0: none), from LAYOUT_SEED (default 20261025, run.py's
# --seed default).

JOBS=${JOBS:-$(($(nproc) / 2))}
LAYOUTS=${LAYOUTS:-0}
LAYOUT_SEED=${LAYOUT_SEED:-20261025}
LLD_DIR=${LLD_DIR:-/usr/lib/llvm-18/bin}
LAYOUT_PY=../common/layout.py  # each build.sh runs in its own folder, bench/<name>
running=""
failed=0

spawn() {
  "$@" &
  running="$running $!"
  # shellcheck disable=SC2086
  set -- $running
  if [ $# -ge "$JOBS" ]; then
    wait "$1" || failed=1
    shift
    running="$*"
  fi
}

finish() {
  for p in $running; do wait "$p" || failed=1; done
  running=""
  [ $failed = 0 ] || { echo "build failed"; exit 1; }
}

make_layouts() {
  [ "$LAYOUTS" -gt 0 ] || return 0
  if [ ! -x "$LLD_DIR/ld.lld" ]; then
    echo "no ld.lld in $LLD_DIR: install lld-18, or set LLD_DIR" >&2
    exit 1
  fi
  rep=0
  while [ $rep -lt "$LAYOUTS" ]; do
    dir=build/layout/$rep
    # shellcheck disable=SC2046
    set -- $(python3 "$LAYOUT_PY" "$LAYOUT_SEED" "$rep")
    mkdir -p "$dir"
    printf '\t.section .text.layout_pad,"ax",@progbits\n\t.space %s\n' "$2" | $CXX -c -x assembler - -o "$dir/pad.o"
    printf '%s\n' -ffunction-sections "-B$LLD_DIR" -fuse-ld=lld "-Wl,--shuffle-sections=.text*=$1" "$dir/pad.o" \
      > "$dir/flags"
    printf '{"seed": %s, "rep": %s, "shuffle_seed": %s, "pad_bytes": %s, "linker": "%s"}\n' \
      "$LAYOUT_SEED" "$rep" "$1" "$2" "$("$LLD_DIR/ld.lld" --version)" > "$dir/layout.json"
    rep=$((rep + 1))
  done
}

# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Sourced by each benchmark's build.sh. It runs the compiles JOBS at a time, and it writes each repetition's code
# layout. It defines three functions:
#
#   spawn <command...>  Runs the command in the background. At most $JOBS commands run at once. The default is half
#                       the cores.
#   finish              Waits for every command that spawn started. If any of them failed, it exits with status 1.
#   make_layouts        Writes build/layout/<rep>/ for each repetition, before any compile reads it. The folder holds
#                       three files:
#                       - pad.o, a padding whose size is the repetition's own, from layout.py.
#                       - flags, a response file for the compiler. It puts every function in a section of its own,
#                         and it links with the lld in $LLD_DIR, by default LLVM 18's, which shuffles the sections by
#                         the repetition's seed. The options are in a file so that no shell expands the glob in the
#                         linker's option.
#                       - layout.json, which run.py checks against its --seed.
#                       A compile reads the options as @build/layout/<rep>/flags.
#
# If LAYOUTS=n is set, the build makes copies for repetitions 0 to n-1. The default, 0, makes none. The layouts follow
# from LAYOUT_SEED, whose default, 20261025, is run.py's default --seed.

JOBS=${JOBS:-$(($(nproc) / 2))}
LAYOUTS=${LAYOUTS:-0}
LAYOUT_SEED=${LAYOUT_SEED:-20261025}
LLD_DIR=${LLD_DIR:-/usr/lib/llvm-18/bin}
LAYOUT_PY=../common/layout.py  # Each build.sh runs in its own folder, bench/<name>.
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

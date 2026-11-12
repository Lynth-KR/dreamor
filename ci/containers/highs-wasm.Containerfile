# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The wasm-highs job's machine for ci/local.py: the wasm job's Emscripten with HiGHS built by it at /opt/highs
# (build-highs-wasm.sh, which CI's wasm-highs job runs too). ci/local.py builds the image again when this file or
# the script changes.
FROM docker.io/emscripten/emsdk:6.0.13
COPY build-highs-wasm.sh /tmp/build-highs-wasm.sh
RUN sh /tmp/build-highs-wasm.sh /opt/highs && rm /tmp/build-highs-wasm.sh

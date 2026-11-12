// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/hand/arm.hpp - the trait of the hand-written arms. Neither this header nor a file that includes it may use a
// dreamor header, because build.sh gives the hand-written build no include path to one.
#pragma once

#include <vector>

#include "../data.hpp"

// The trait that data.hpp describes. A row holds a pointer into the std::vector of its column.
struct HandArm {
  using Ents   = std::vector<Ent>;
  using Handle = const Ent*;

  static void add(Ents& ents, const Ent& e) { ents.push_back(e); }

  static void freeze(Ents&) {}

  static Handle handle(const Ents&, const Ent& e) { return &e; }
};

using Ents    = HandArm::Ents;
using Handle  = HandArm::Handle;
using Data    = DataOf<HandArm>;
using Samples = SamplesOf<HandArm>;

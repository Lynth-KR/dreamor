// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: EntityVector<T> holds each entity once by its primary key
#include "prelude.hpp"

struct Unkeyed {
  std::size_t id;
};

const dreamor::EntityVector<Unkeyed> unkeyed;

// SPDX-License-Identifier: Apache-2.0
// expect: EntityVector<T> holds each entity once by its primary key
#include "prelude.hpp"

struct Unkeyed {
  std::size_t id;
};

const dreamor::EntityVector<Unkeyed> unkeyed;

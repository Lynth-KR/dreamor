// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The HiGHS backend, on HiGHS's own C++ API. Its parts are in internal/highs/.
#pragma once

// A user may rely on this include, because a model needs its sets. In the same way, <iostream> is guaranteed to
// include <istream> and <ostream>.
#include "dreamor/index_set.hpp"
#include "dreamor/internal/highs/environment.hpp"
#include "dreamor/internal/highs/error.hpp"
#include "dreamor/internal/highs/expression.hpp"
#include "dreamor/internal/highs/model.hpp"
#include "dreamor/internal/highs/operators.hpp"

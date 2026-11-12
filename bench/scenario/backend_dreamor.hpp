// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/backend_dreamor.hpp - backend.hpp for the dreamor arm, which <scenario>_dreamor.cpp builds.
//
// It adds the backend's dreamor header, includes the third-party headers inside the macros that dreamor wraps them
// in, and holds what only the dreamor arm hands a solver. The hand-written arms include backend.hpp alone.
#pragma once

#if defined(SCENARIO_MATHOPT)
#  include "dreamor/internal/base/third_party_headers.hpp"
#  include "dreamor/mathopt.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#  include "ortools/math_opt/cpp/math_opt.h"
#  include "ortools/math_opt/io/mps_converter.h"
DREAMOR_END_THIRD_PARTY_HEADERS
#elif defined(SCENARIO_HIGHS)
#  include "dreamor/highs.hpp"
#else
#  include <ilcplex/ilocplex.h>
#  include "dreamor/concert.hpp"
#endif
#include "backend.hpp"

#if defined(SCENARIO_HIGHS)
// Hands the dreamor arm's model to HiGHS and starts HiGHS. The HighsEnv hands the model over in one passModel when
// highs() is first called, so the extract phase times that call.
inline void solve(dreamor::highs::HighsEnv& env, Result& r) {
  Highs* highs = nullptr;
  r.measure("extract", [&] { highs = &env.highs(); });
  start(*highs, r);
}
#endif

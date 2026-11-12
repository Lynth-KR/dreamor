// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A stand-in for <ilcplex/ilocplex.h>. As in real CPLEX, it includes the Concert headers.
//
// It declares no IloCplex, because the stub has no solver.
#pragma once
#include <ilconcert/iloexpression.h>
#include <ilconcert/ilomodel.h>

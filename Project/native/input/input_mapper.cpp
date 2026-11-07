// SPDX-License-Identifier: GPL-2.0-or-later
// native/input/input_mapper.cpp
// Uncharted: Sic Parvis Magna Collection — Stateless Input Coordinate Normalization Implementation

#include "unc/input/input_mapper.hpp"
#include "unc/input.hpp"

namespace unc::input {

// Explicit translation unit verification for InputMapper static invariants.
static_assert(sizeof(NormalizedGameCoords) == 16, "NormalizedGameCoords must be 2x IEEE-754 doubles");
static_assert(alignof(InputMapper) == 64, "InputMapper must be 64-byte cache-line aligned");

} // namespace unc::input

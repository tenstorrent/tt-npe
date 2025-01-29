
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <variant>

#include "npeCommon.hpp"
#include "npeStats.hpp"

namespace tt_npe {
// empty result indicates failure
using npeResult = std::variant<npeException, npeStats>;
}  // namespace tt_npe
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once
#include "npeConfig.hpp"

namespace tt_npe {
// parse cli options and populate npe config; returns true if successful
bool parse_options(tt_npe::npeConfig& npe_config, int argc, char** argv);
}  // namespace tt_npe
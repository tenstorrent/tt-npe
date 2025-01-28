// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include "npeWorkload.hpp"

namespace tt_npe {

std::optional<npeWorkload> ingestJSONWorkload(
    const std::string &wl_filename, bool verbose = false);

}

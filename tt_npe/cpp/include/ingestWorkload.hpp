// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include "npeWorkload.hpp"

namespace tt_npe {

std::optional<npeWorkload> createWorkloadFromJSON(
    const std::string &wl_filename, const std::string &device_name, bool is_tt_metal_trace_format, bool verbose = false, int64_t kernel_duration_threshold = 1000000);

}  // namespace tt_npe
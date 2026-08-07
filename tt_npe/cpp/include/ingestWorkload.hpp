// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <limits>

#include "npeWorkload.hpp"

namespace tt_npe {

// Inclusive window of cycles used to filter noc trace events during ingest. Bounds are
// relative to the first event timestamp in the trace (i.e. the same cycle domain as the
// op duration reported by npeWorkload::getGoldenResultCycles()).
struct CycleWindow {
    Cycle start = 0;
    Cycle end = std::numeric_limits<Cycle>::max();

    bool isUnbounded() const { return start == 0 && end == std::numeric_limits<Cycle>::max(); }
    bool contains(Cycle cycle) const { return cycle >= start && cycle <= end; }
    bool isValid() const { return start <= end; }
    std::string to_string() const {
        return fmt::format(
            "[{},{}]",
            start,
            end == std::numeric_limits<Cycle>::max() ? std::string("end") : std::to_string(end));
    }
};

std::optional<npeWorkload> createWorkloadFromJSON(
    const std::string &wl_filename,
    const std::string &device_name,
    bool is_tt_metal_trace_format,
    bool verbose = false,
    const CycleWindow &cycle_window = {});

}  // namespace tt_npe

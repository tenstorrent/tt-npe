// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#pragma once

#include <cstddef>
#include <map>
#include <ranges>
#include <stdexcept>
#include <vector>

#include "npeCommon.hpp"

namespace tt_npe {

struct TimelineTransferInterval {
    int component_id;
    int logical_id;
    Cycle start_cycle;
    Cycle end_cycle;
};

struct TimelineTimestepRange {
    size_t start;
    size_t end;
};

template <typename Container>
auto timelineTimestepSubrange(
    const Container& timesteps,
    TimelineTimestepRange range) {
    if (range.start > range.end || range.end > timesteps.size()) {
        throw std::out_of_range("Invalid timeline timestep range");
    }
    return std::ranges::subrange(
        timesteps.begin() + range.start, timesteps.begin() + range.end);
}

class TimelineActiveTransferSweep {
public:
    explicit TimelineActiveTransferSweep(std::vector<TimelineTransferInterval> intervals);

    void update(Cycle timestep_start, Cycle timestep_end);

    template <typename Callback>
    void forEachActiveTransfer(Callback&& callback) const {
        for (const auto& active_transfer : active_refcounts_) {
            callback(active_transfer.first);
        }
    }

private:
    struct Event {
        Cycle cycle;
        int logical_id;
    };

    std::vector<Event> starts_;
    std::vector<Event> ends_;
    std::map<int, size_t> active_refcounts_;
    size_t next_start_ = 0;
    size_t next_end_ = 0;
    Cycle last_timestep_start_ = 0;
    Cycle last_timestep_end_ = 0;
    bool has_previous_timestep_ = false;
};

}  // namespace tt_npe

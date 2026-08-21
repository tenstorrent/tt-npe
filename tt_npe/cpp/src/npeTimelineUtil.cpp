// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include "npeTimelineUtil.hpp"

#include <algorithm>

namespace tt_npe {

TimelineActiveTransferSweep::TimelineActiveTransferSweep(
    std::vector<TimelineTransferInterval> intervals) {
    starts_.reserve(intervals.size());
    ends_.reserve(intervals.size());
    for (const auto& interval : intervals) {
        starts_.push_back({interval.start_cycle, interval.logical_id});
        ends_.push_back({interval.end_cycle, interval.logical_id});
    }
    auto by_cycle = [](const Event& lhs, const Event& rhs) {
        return lhs.cycle < rhs.cycle;
    };
    std::stable_sort(starts_.begin(), starts_.end(), by_cycle);
    std::stable_sort(ends_.begin(), ends_.end(), by_cycle);
}

void TimelineActiveTransferSweep::update(Cycle timestep_start, Cycle timestep_end) {
    if (has_previous_timestep_ &&
        (timestep_start < last_timestep_start_ || timestep_end < last_timestep_end_)) {
        active_refcounts_.clear();
        next_start_ = 0;
        next_end_ = 0;
    }
    has_previous_timestep_ = true;
    last_timestep_start_ = timestep_start;
    last_timestep_end_ = timestep_end;

    while (next_start_ < starts_.size() && starts_[next_start_].cycle <= timestep_end) {
        ++active_refcounts_[starts_[next_start_].logical_id];
        ++next_start_;
    }
    while (next_end_ < ends_.size() && ends_[next_end_].cycle <= timestep_start) {
        auto active = active_refcounts_.find(ends_[next_end_].logical_id);
        if (active != active_refcounts_.end() && --active->second == 0) {
            active_refcounts_.erase(active);
        }
        ++next_end_;
    }
}

}  // namespace tt_npe

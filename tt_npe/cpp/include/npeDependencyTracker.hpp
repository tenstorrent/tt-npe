// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <cstdint>
#include <vector>

#include "npeAssert.hpp"
#include "npeCommon.hpp"

namespace tt_npe {
using npeCheckpointID = int32_t;

class npeTransferDependencyTracker {
   public:
    npeCheckpointID createCheckpoint(uint32_t total_dep_count, CycleCount delay) {
        checkpoints.emplace_back(total_dep_count, checkpoints.size(), delay);
        return checkpoints.back().id;
    }

    static constexpr npeCheckpointID UNDEFINED_CHECKPOINT = -1;
    bool defined(npeCheckpointID id) { return id != UNDEFINED_CHECKPOINT; }

    // increments dep counter for checkpoint
    void updateCheckpoint(npeCheckpointID id, CycleCount end_cycle) {
        if (!defined(id))
            return;
        TT_ASSERT(id < checkpoints.size());
        auto& cp = checkpoints[id];
        cp.dep_completed++;
        cp.end_cycle = std::max(cp.end_cycle, end_cycle);
    }

    uint32_t end_cycle(npeCheckpointID id) {
        if (!defined(id))
            return 0;
        TT_ASSERT(id < checkpoints.size());
        return checkpoints[id].end_cycle;
    }

    uint32_t end_cycle_plus_delay(npeCheckpointID id) {
        if (!defined(id))
            return 0;
        TT_ASSERT(id < checkpoints.size());
        return checkpoints[id].end_cycle + checkpoints[id].delay;
    }

    bool done(npeCheckpointID id, CycleCount curr_cycle) {
        if (!defined(id))
            return true;
        TT_ASSERT(id < checkpoints.size());
        return checkpoints[id].done(curr_cycle);
    }

    // check that current state is self-consistent
    bool sanityCheck() const {
        for (const auto& c : checkpoints) {
            if (c.dep_completed > c.dep_total) {
                return false;
            }
        }
        return true;
    }

    // returns true if all checkpoints are complete
    bool allComplete() const {
        for (const auto& c : checkpoints) {
            if (c.dep_completed != c.dep_total) {
                return false;
            }
        }
        return true;
    }

    // resets all checkpoints to original state
    void reset() {
        for (auto& c : checkpoints) {
            c.dep_completed = 0;
        }
    }

   private:
    struct npeCheckpoint {
        npeCheckpoint(uint32_t total_dep_count, npeCheckpointID id, CycleCount delay) :
            dep_completed(0), dep_total(total_dep_count), id(id), end_cycle(0), delay(delay) {}

        // returns true if all dependencies are completed
        bool allDepsComplete() const { return dep_completed == dep_total; }

        // returns true if all dependencies are completed and delay has elapsed
        bool done(CycleCount cycle) const {
            return allDepsComplete() && cycle >= end_cycle + delay;
        }

        uint32_t dep_completed;
        uint32_t dep_total;
        CycleCount end_cycle;
        CycleCount delay;
        npeCheckpointID id;
    };

    std::vector<npeCheckpoint> checkpoints;
};
}  // namespace tt_npe

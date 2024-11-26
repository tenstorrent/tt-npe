#pragma once

#include <vector>

#include "fmt/core.h"
#include "npeDeviceModel.hpp"
#include "util.hpp"

namespace tt_npe {

using nocWorkloadPhaseID = int;
using nocWorkloadTransferID = int;

struct nocWorkloadTransfer {
    uint32_t packet_size;
    uint32_t num_packets;
    Coord src, dst;
    float injection_rate = 28.1;  // how many GB/cycle the source can inject
    CycleCount cycle_offset = 0;  // when this transfer can start relative to beginning of its phase
    nocWorkloadPhaseID phase_id = -1;
    nocWorkloadTransferID id = -1;

    // returns true if the transfer appears well formed
    bool validate(size_t device_num_rows, size_t device_num_cols) const;
};

struct nocWorkloadPhase {
    nocWorkloadPhaseID id = -1;
    std::vector<nocWorkloadTransfer> transfers;
};

class nocWorkload {
   public:
    nocWorkloadPhaseID addPhase(nocWorkloadPhase phase);
    const auto &getPhases() const { return phases; };

    bool validate(const nocModel &noc_model, bool verbose = true) const;

   private:
    std::vector<nocWorkloadPhase> phases;
    nocWorkloadTransferID gbl_transfer_id = 0;
};

}  // namespace tt_npe

#pragma once

#include <vector>

#include "fmt/core.h"
#include "npeDeviceModel.hpp"
#include "util.hpp"

namespace tt_npe {

using npeWorkloadPhaseID = int;
using npeWorkloadTransferID = int;

struct npeWorkloadTransfer {
    uint32_t packet_size;
    uint32_t num_packets;
    Coord src, dst;
    float injection_rate = 28.1;  // how many GB/cycle the source can inject
    CycleCount cycle_offset = 0;  // when this transfer can start relative to beginning of its phase
    npeWorkloadPhaseID phase_id = -1;
    npeWorkloadTransferID id = -1;

    // returns true if the transfer appears well formed
    bool validate(size_t device_num_rows, size_t device_num_cols) const;
};

struct npeWorkloadPhase {
    npeWorkloadPhaseID id = -1;
    std::vector<npeWorkloadTransfer> transfers;
};

class npeWorkload {
   public:
    npeWorkloadPhaseID addPhase(npeWorkloadPhase phase);
    const auto &getPhases() const { return phases; };

    bool validate(const npeDeviceModel &noc_model, bool verbose = true) const;

   private:
    std::vector<npeWorkloadPhase> phases;
    npeWorkloadTransferID gbl_transfer_id = 0;
};

}  // namespace tt_npe

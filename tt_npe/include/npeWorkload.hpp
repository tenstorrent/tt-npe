#pragma once

#include <vector>

#include "fmt/core.h"
#include "npeDeviceModel.hpp"
#include "util.hpp"

namespace tt_npe {

using npeWorkloadPhaseID = int;
using npeWorkloadTransferID = int;

class npeWorkload;

struct npeWorkloadTransfer {
    friend npeWorkload;

    npeWorkloadTransfer() = default;
    npeWorkloadTransfer(
        uint32_t packet_size_arg,
        uint32_t num_packets_arg,
        Coord src_arg,
        Coord dst_arg,
        float injection_rate_arg,
        CycleCount phase_cycle_offset_arg,
        nocType noc_type) :
        packet_size(packet_size_arg),
        num_packets(num_packets_arg),
        src(src_arg),
        dst(dst_arg),
        injection_rate(injection_rate_arg),
        phase_cycle_offset(phase_cycle_offset_arg),
        noc_type(noc_type),
        total_bytes(packet_size_arg * num_packets_arg),
        phase_id(-1),
        id(-1) {}

    uint32_t packet_size;
    uint32_t num_packets;
    Coord src, dst;
    float injection_rate = 28.1;  // how many GB/cycle the source can inject
    CycleCount phase_cycle_offset = 0;  // when this transfer can start relative to beginning of its phase
    nocType noc_type;
    uint32_t total_bytes;

    // returns true if sanity checks pass
    bool validate(size_t device_num_rows, size_t device_num_cols, bool verbose) const;

    npeWorkloadTransferID getID() const { return id; }
    npeWorkloadPhaseID getPhaseID() const { return phase_id; }

   private:
    npeWorkloadPhaseID phase_id = -1;
    npeWorkloadTransferID id = -1;
};

struct npeWorkloadPhase {
    friend npeWorkload;

    std::vector<npeWorkloadTransfer> transfers;

    npeWorkloadTransferID getID() const { return id; }

   private:
    // id should not be set by user
    npeWorkloadPhaseID id = -1;
};

class npeWorkload {
   public:
    npeWorkloadPhaseID addPhase(npeWorkloadPhase phase);
    const auto &getPhases() const { return phases; };

    // returns true if workload passes all sanity checks
    bool validate(const npeDeviceModel &noc_model, bool verbose = true) const;

    // sets injection rate for each transfer in workload based on src core type
    void inferInjectionRates(const npeDeviceModel& device_model);

   private:
    std::vector<npeWorkloadPhase> phases;
    npeWorkloadTransferID gbl_transfer_id = 0;
};

}  // namespace tt_npe

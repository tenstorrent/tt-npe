// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <unordered_map>
#include <vector>
#include <filesystem>
#include <optional>

#include "fmt/core.h"
#include "npeCommon.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

using npeWorkloadPhaseID = int;
using npeWorkloadTransferID = int;
using npeWorkloadTransferGroupID = int;
using npeWorkloadTransferGroupIndex = int;
using npeWorkloadTransferGroupParent = int;

class npeWorkload;
class npeDeviceModel;

struct npeWorkloadTransfer {
    friend npeWorkload;

    npeWorkloadTransfer() = default;
    npeWorkloadTransfer(
        uint32_t packet_size_arg,
        uint32_t num_packets_arg,
        Coord src_arg,
        NocDestination dst_arg,
        float injection_rate_arg,
        CycleCount phase_cycle_offset_arg,
        nocType noc_type,
        std::string_view noc_event_type = "",
        npeWorkloadTransferGroupID transfer_group_id_arg = -1,
        npeWorkloadTransferGroupIndex transfer_group_index_arg = -1,
        npeWorkloadTransferGroupParent transfer_group_parent_arg = -1) :
        packet_size(packet_size_arg),
        num_packets(num_packets_arg),
        src(src_arg),
        dst(dst_arg),
        injection_rate(injection_rate_arg),
        phase_cycle_offset(phase_cycle_offset_arg),
        noc_type(noc_type),
        noc_event_type(noc_event_type),
        total_bytes(packet_size_arg * num_packets_arg),
        transfer_group_id(transfer_group_id_arg),
        transfer_group_index(transfer_group_index_arg),
        transfer_group_parent(transfer_group_parent_arg),
        phase_id(-1),
        id(-1) {}

    uint32_t packet_size;
    uint32_t num_packets;
    Coord src;
    NocDestination dst;
    float injection_rate = 28.1;  // how many GB/cycle the source can inject
    CycleCount phase_cycle_offset =
        0;  // when this transfer can start relative to beginning of its phase
    nocType noc_type;
    std::string noc_event_type;
    uint32_t total_bytes;

    npeWorkloadTransferGroupID transfer_group_id = -1;
    npeWorkloadTransferGroupIndex transfer_group_index = -1;
    npeWorkloadTransferGroupParent transfer_group_parent = -1;

    // returns true if sanity checks pass
    bool validate(const npeDeviceModel &device_model, const std::optional<std::filesystem::path>& source_file, bool verbose) const;

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
    bool validate(const npeDeviceModel &device_model, bool verbose = true) const;

    // sets injection rate for each transfer in workload based on src core type
    void inferInjectionRates(const npeDeviceModel &device_model);

    // scales phase offsets linearly; allows compressing/expanding workload schedule 
    void scaleWorkloadSchedule(float scale_factor);

    std::pair<CycleCount, CycleCount> getGoldenResultCycles(DeviceID device_id) const { return golden_cycles.at(device_id); }
    void setGoldenResultCycles(DeviceID device_id, CycleCount start_cycle, CycleCount end_cycle) { 
        golden_cycles[device_id] = {start_cycle, end_cycle}; 
    }

    npeWorkload removeLocalUnicastTransfers() const;

    npeWorkloadTransferGroupID registerTransferGroupID() { 
        return num_transfer_groups++;
    }
    int getNumTransferGroups() const { return num_transfer_groups; } 

    std::optional<std::filesystem::path> getSourceFilePath() const { return source_filepath; }
    void setSourceFilePath(const std::filesystem::path &filepath) { source_filepath = filepath; }

   private:
    std::optional<std::filesystem::path> source_filepath;
    std::vector<npeWorkloadPhase> phases;
    npeWorkloadTransferID gbl_transfer_id = 0;
    npeWorkloadTransferGroupID num_transfer_groups = 0;
    std::unordered_map<DeviceID, std::pair<CycleCount, CycleCount>> golden_cycles;
};

}  // namespace tt_npe

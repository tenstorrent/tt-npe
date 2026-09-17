// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#pragma once

#include <filesystem>

#include "npeCommon.hpp"
#include "npeDeviceTypes.hpp"

namespace tt_npe {

struct NpeReadLatencyConfig {
    Cycle same_tile = 0;
    Cycle same_col = 0;
    Cycle same_row = 0;
    Cycle diagonal = 0;
};

struct NpeWriteLatencyConfig {
    Cycle startup = 0;
    Cycle cycles_per_hop = 0;
};

struct NpeDeviceModelConfig {
    BytesPerCycle link_bandwidth = 0;
    BytesPerCycle eth_bandwidth_per_link = 0;
    size_t dram_channels_per_controller = 0;
    CoreTypeToInjectionRate injection_rates;
    CoreTypeToAbsorptionRate absorption_rates;
    TransferBandwidthTable transfer_bandwidth_table;
    NpeReadLatencyConfig read_latencies;
    NpeWriteLatencyConfig write_latencies;
};

NpeDeviceModelConfig parseNpeDeviceModelConfig(
    const std::filesystem::path& filepath);

}  // namespace tt_npe

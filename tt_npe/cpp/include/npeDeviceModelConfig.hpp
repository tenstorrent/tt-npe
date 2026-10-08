// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#pragma once

#include <filesystem>

#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeDeviceTypes.hpp"
#include "npeSocDescriptor.hpp"

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

enum class NocTopology { Torus, Mesh };

// Torus: NoC 0 routes east then south, NoC 1 north then west, with wraparound.
// XY: a single NoC on a mesh routes along X, then Y, without wraparound.
enum class NocRouting { Torus, XY };

struct NpeNocConfig {
    NocTopology topology = NocTopology::Torus;
    NocRouting routing = NocRouting::Torus;
    size_t num_nocs = 2;
};

struct NpeDeviceModelConfig {
    BytesPerCycle link_bandwidth = 0;
    BytesPerCycle eth_bandwidth_per_link = 0;
    size_t dram_channels_per_controller = 0;
    NpeNocConfig noc;
    CoreTypeToInjectionRate injection_rates;
    CoreTypeToAbsorptionRate absorption_rates;
    TransferBandwidthTable transfer_bandwidth_table;
    NpeReadLatencyConfig read_latencies;
    NpeWriteLatencyConfig write_latencies;
};

struct ResolvedNpeDeviceModelConfig {
    DeviceArch arch;
    SocDescriptor soc_descriptor;
    NpeDeviceModelConfig model_config;
    std::filesystem::path soc_descriptor_path;
    std::filesystem::path model_config_path;
};

NpeDeviceModelConfig parseNpeDeviceModelConfig(
    const std::filesystem::path& filepath);

ResolvedNpeDeviceModelConfig resolveNpeDeviceModelConfig(
    const std::filesystem::path& soc_descriptor_path,
    const std::filesystem::path& model_config_directory = {});

}  // namespace tt_npe

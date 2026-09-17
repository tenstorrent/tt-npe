// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include "npeDeviceModelConfig.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

#include <yaml-cpp/yaml.h>

namespace tt_npe {
namespace {

const YAML::Node requireNode(
    const YAML::Node& parent,
    std::string_view key,
    const std::filesystem::path& filepath) {
    const auto node = parent[std::string(key)];
    if (!node.IsDefined()) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Missing required field '{}' in NPE device model config file '{}'",
                key,
                filepath.string()));
    }
    return node;
}

std::string uppercase(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
    return value;
}

CoreType parseCoreType(const std::string& name, const std::filesystem::path& filepath) {
    const auto normalized = uppercase(name);
    if (normalized == "WORKER") {
        return CoreType::WORKER;
    }
    if (normalized == "DRAM") {
        return CoreType::DRAM;
    }
    if (normalized == "ETH") {
        return CoreType::ETH;
    }
    if (normalized == "UNDEF") {
        return CoreType::UNDEF;
    }
    throw npeException(
        npeErrorCode::DEVICE_MODEL_INIT_FAILED,
        fmt::format(
            "Unknown core type '{}' in NPE device model config file '{}'",
            name,
            filepath.string()));
}

template <typename RateMap>
RateMap parseRates(
    const YAML::Node& root,
    std::string_view field,
    const std::filesystem::path& filepath) {
    const auto node = requireNode(root, field, filepath);
    if (!node.IsMap()) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Field '{}' must be a map in NPE device model config file '{}'",
                field,
                filepath.string()));
    }

    RateMap rates;
    for (const auto& entry : node) {
        const auto core_type = parseCoreType(entry.first.as<std::string>(), filepath);
        const auto rate = entry.second.as<BytesPerCycle>();
        if (rate < 0) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Field '{}.{}' must be non-negative in NPE device model config file '{}'",
                    field,
                    entry.first.as<std::string>(),
                    filepath.string()));
        }
        rates[core_type] = rate;
    }

    constexpr std::array required_core_types = {
        CoreType::UNDEF, CoreType::WORKER, CoreType::DRAM, CoreType::ETH};
    for (const auto core_type : required_core_types) {
        if (!rates.contains(core_type)) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Field '{}' is missing rate for core type '{}' in NPE device model config file '{}'",
                    field,
                    magic_enum::enum_name(core_type),
                    filepath.string()));
        }
    }
    return rates;
}

TransferBandwidthTable parseTransferBandwidthTable(
    const YAML::Node& root, const std::filesystem::path& filepath) {
    const auto node = requireNode(root, "transfer_bandwidth_table", filepath);
    if (!node.IsSequence() || node.size() == 0) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Field 'transfer_bandwidth_table' must be a non-empty sequence in '{}'",
                filepath.string()));
    }

    TransferBandwidthTable table;
    table.reserve(node.size());
    for (const auto& entry : node) {
        if (!entry.IsSequence() || entry.size() != 2) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Each transfer bandwidth entry must be [packet_size, bandwidth] in '{}'",
                    filepath.string()));
        }
        const auto packet_size = entry[0].as<size_t>();
        const auto bandwidth = entry[1].as<BytesPerCycle>();
        if (bandwidth < 0) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Transfer bandwidth must be non-negative in '{}'", filepath.string()));
        }
        if (!table.empty() && packet_size <= table.back().first) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Transfer bandwidth packet sizes must be strictly increasing in '{}'",
                    filepath.string()));
        }
        table.emplace_back(packet_size, bandwidth);
    }
    return table;
}

}  // namespace

NpeDeviceModelConfig parseNpeDeviceModelConfig(
    const std::filesystem::path& filepath) {
    try {
        const auto root = YAML::LoadFile(filepath.string());
        if (!root.IsMap()) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "NPE device model config file '{}' must contain a YAML map",
                    filepath.string()));
        }

        NpeDeviceModelConfig config;
        config.link_bandwidth =
            requireNode(root, "link_bandwidth", filepath).as<BytesPerCycle>();
        config.eth_bandwidth_per_link =
            requireNode(root, "eth_bandwidth_per_link", filepath).as<BytesPerCycle>();
        config.dram_channels_per_controller =
            requireNode(root, "dram_channels_per_controller", filepath).as<size_t>();
        if (config.link_bandwidth <= 0 || config.eth_bandwidth_per_link < 0 ||
            config.dram_channels_per_controller == 0) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Bandwidths and DRAM channel count are invalid in '{}'", filepath.string()));
        }

        config.injection_rates =
            parseRates<CoreTypeToInjectionRate>(root, "injection_rates", filepath);
        config.absorption_rates =
            parseRates<CoreTypeToAbsorptionRate>(root, "absorption_rates", filepath);
        config.transfer_bandwidth_table = parseTransferBandwidthTable(root, filepath);

        const auto latencies = requireNode(root, "latencies", filepath);
        const auto read = requireNode(latencies, "read", filepath);
        config.read_latencies.same_tile =
            requireNode(read, "same_tile", filepath).as<Cycle>();
        config.read_latencies.same_col =
            requireNode(read, "same_col", filepath).as<Cycle>();
        config.read_latencies.same_row =
            requireNode(read, "same_row", filepath).as<Cycle>();
        config.read_latencies.diagonal =
            requireNode(read, "diagonal", filepath).as<Cycle>();

        const auto write = requireNode(latencies, "write", filepath);
        config.write_latencies.startup =
            requireNode(write, "startup", filepath).as<Cycle>();
        config.write_latencies.cycles_per_hop =
            requireNode(write, "cycles_per_hop", filepath).as<Cycle>();

        return config;
    } catch (const npeException&) {
        throw;
    } catch (const YAML::Exception& error) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Failed to parse NPE device model config file '{}': {}",
                filepath.string(),
                error.what()));
    }
}

}  // namespace tt_npe

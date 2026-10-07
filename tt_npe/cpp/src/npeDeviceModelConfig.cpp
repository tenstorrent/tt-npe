// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include "npeDeviceModelConfig.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

NpeNocConfig parseNocConfig(
    const YAML::Node& root, const std::filesystem::path& filepath) {
    NpeNocConfig noc;
    const auto node = root["noc"];
    if (!node.IsDefined()) {
        return noc;
    }
    if (!node.IsMap()) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Field 'noc' must be a map in NPE device model config file '{}'",
                filepath.string()));
    }

    auto invalid = [&filepath](std::string_view message) {
        return npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "{} in NPE device model config file '{}'", message, filepath.string()));
    };

    const auto topology = requireNode(node, "topology", filepath).as<std::string>();
    if (uppercase(topology) == "TORUS") {
        noc.topology = NocTopology::Torus;
    } else if (uppercase(topology) == "MESH") {
        noc.topology = NocTopology::Mesh;
    } else {
        throw invalid(fmt::format("Unknown noc.topology '{}'", topology));
    }

    const auto routing = requireNode(node, "routing", filepath).as<std::string>();
    if (uppercase(routing) == "TORUS") {
        noc.routing = NocRouting::Torus;
    } else if (uppercase(routing) == "XY") {
        noc.routing = NocRouting::XY;
    } else {
        throw invalid(fmt::format("Unknown noc.routing '{}'", routing));
    }

    noc.num_nocs = requireNode(node, "num_nocs", filepath).as<size_t>();
    noc.physical_channels =
        requireNode(node, "physical_channels", filepath).as<size_t>();
    if (noc.physical_channels == 0) {
        throw invalid("Field 'noc.physical_channels' must be at least 1");
    }

    if (noc.routing == NocRouting::Torus &&
        (noc.topology != NocTopology::Torus || noc.num_nocs != 2)) {
        throw invalid("noc.routing 'torus' requires noc.topology 'torus' and noc.num_nocs 2");
    }
    if (noc.routing == NocRouting::XY &&
        (noc.topology != NocTopology::Mesh || noc.num_nocs != 1)) {
        throw invalid("noc.routing 'xy' requires noc.topology 'mesh' and noc.num_nocs 1");
    }
    return noc;
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
        config.noc = parseNocConfig(root, filepath);

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

namespace {

std::string normalizeDeviceArchName(std::string_view arch_name) {
    const auto first = std::find_if_not(
        arch_name.begin(), arch_name.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(
                          arch_name.rbegin(),
                          arch_name.rend(),
                          [](unsigned char c) { return std::isspace(c); })
                          .base();
    if (first >= last) {
        return {};
    }

    std::string normalized(first, last);
    std::transform(
        normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return normalized;
}

// Returns the DeviceArch and the model config file name (without extension).
std::pair<DeviceArch, std::string> resolveDeviceArch(
    std::string_view arch_name, const std::filesystem::path& soc_descriptor_path) {
    const auto normalized = normalizeDeviceArchName(arch_name);
    if (normalized == "blackhole") {
        return {DeviceArch::Blackhole, "blackhole"};
    }
    // tt-metal descriptors use WORMHOLE_B0; older descriptors may use WORMHOLE.
    if (normalized == "wormhole_b0" || normalized == "wormhole") {
        return {DeviceArch::WormholeB0, "wormhole_b0"};
    }
    if (normalized == "quasar") {
        return {DeviceArch::Quasar, "quasar"};
    }
    throw npeException(
        npeErrorCode::DEVICE_MODEL_INIT_FAILED,
        fmt::format(
            "SOC descriptor '{}' has unsupported arch_name '{}'",
            soc_descriptor_path.string(),
            arch_name));
}

std::filesystem::path requireModelConfigDirectory(
    const std::filesystem::path& directory, std::string_view source) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "NPE device model config directory from {} does not exist: '{}'",
                source,
                directory.string()));
    }
    return directory;
}

std::filesystem::path resolveModelConfigDirectory(
    const std::filesystem::path& explicit_directory) {
    if (!explicit_directory.empty()) {
        return requireModelConfigDirectory(explicit_directory, "explicit override");
    }

    if (const char* environment_directory =
            std::getenv("TT_NPE_DEVICE_MODEL_CONFIG_DIR");
        environment_directory != nullptr && environment_directory[0] != '\0') {
        return requireModelConfigDirectory(
            environment_directory, "TT_NPE_DEVICE_MODEL_CONFIG_DIR");
    }

#ifdef TT_NPE_SOURCE_MODEL_CONFIG_DIR
    return requireModelConfigDirectory(
        TT_NPE_SOURCE_MODEL_CONFIG_DIR, "tt-npe source tree");
#else
    throw npeException(
        npeErrorCode::DEVICE_MODEL_INIT_FAILED,
        "Could not locate NPE device model configs; set TT_NPE_DEVICE_MODEL_CONFIG_DIR");
#endif
}

}  // namespace

ResolvedNpeDeviceModelConfig resolveNpeDeviceModelConfig(
    const std::filesystem::path& soc_descriptor_path,
    const std::filesystem::path& model_config_directory) {
    const auto soc_descriptor = parseSocDescriptor(soc_descriptor_path.string());
    if (!soc_descriptor.has_value()) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Could not load SOC descriptor '{}'", soc_descriptor_path.string()));
    }

    const auto [arch, model_config_name] =
        resolveDeviceArch(soc_descriptor->arch_name, soc_descriptor_path);
    const auto model_config_path =
        resolveModelConfigDirectory(model_config_directory) / (model_config_name + ".yaml");
    std::error_code error;
    if (!std::filesystem::is_regular_file(model_config_path, error)) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "No NPE device model config found for architecture '{}' at '{}'",
                model_config_name,
                model_config_path.string()));
    }

    return {
        .arch = arch,
        .soc_descriptor = *soc_descriptor,
        .model_config = parseNpeDeviceModelConfig(model_config_path),
        .soc_descriptor_path = soc_descriptor_path,
        .model_config_path = model_config_path};
}

}  // namespace tt_npe

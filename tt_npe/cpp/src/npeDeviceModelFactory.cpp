// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include "npeDeviceModelFactory.hpp"

#include <boost/unordered/unordered_flat_set.hpp>
#include <charconv>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

#include "device_models/blackhole.hpp"
#include "device_models/blackhole_multichip.hpp"
#include "device_models/custom.hpp"
#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "nlohmann/json.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModelConfigResolver.hpp"

namespace tt_npe {
namespace {

DeviceID parseTopologyDeviceID(
    std::string_view value, const std::filesystem::path& topology_path) {
    int64_t parsed_value = -1;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed_value);
    if (result.ec != std::errc() ||
        result.ptr != value.data() + value.size() || parsed_value < 0 ||
        parsed_value > std::numeric_limits<DeviceID>::max()) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Invalid device ID '{}' in topology '{}'",
                value,
                topology_path.string()));
    }
    return static_cast<DeviceID>(parsed_value);
}

boost::unordered_flat_set<DeviceID> parseTopologyDeviceIDs(
    const std::filesystem::path& topology_path) {
    std::ifstream input(topology_path);
    if (!input.is_open()) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format("Could not open topology '{}'", topology_path.string()));
    }

    nlohmann::json topology;
    try {
        topology = nlohmann::json::parse(input);
    } catch (const nlohmann::json::exception& exception) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Could not parse topology '{}': {}",
                topology_path.string(),
                exception.what()));
    }

    if (!topology.contains("device_id_to_fabric_node_id") ||
        !topology["device_id_to_fabric_node_id"].is_object() ||
        topology["device_id_to_fabric_node_id"].empty()) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Topology '{}' has no device_id_to_fabric_node_id mapping",
                topology_path.string()));
    }

    boost::unordered_flat_set<DeviceID> device_ids;
    for (const auto& [device_id, unused] :
         topology["device_id_to_fabric_node_id"].items()) {
        static_cast<void>(unused);
        device_ids.insert(parseTopologyDeviceID(device_id, topology_path));
    }
    return device_ids;
}

}  // namespace

std::unique_ptr<npeDeviceModel> npeDeviceModelFactory::createDeviceModel(
    const npeConfig& cfg,
    const std::filesystem::path& model_config_directory) {
    if (cfg.soc_descriptor_file.empty()) {
        return createDeviceModel(cfg.device_name);
    }

    auto resolved_config = resolveNpeDeviceModelConfig(
        cfg.soc_descriptor_file, model_config_directory);
    if (cfg.topology_json.empty()) {
        return std::make_unique<CustomDeviceModel>(
            std::move(resolved_config), getNumChips(cfg.device_name));
    }

    auto device_ids = parseTopologyDeviceIDs(cfg.topology_json);
    return std::make_unique<CustomDeviceModel>(
        std::move(resolved_config), std::move(device_ids));
}

std::unique_ptr<npeDeviceModel> npeDeviceModelFactory::createDeviceModel(
    const std::string& device_name) {
    if (device_name == "wormhole_b0" || device_name == "N150") {
        return std::make_unique<WormholeB0DeviceModel>();
    }
    if (device_name == "N300") {
        return std::make_unique<WormholeMultichipDeviceModel>(2);
    }
    if (device_name == "T3K") {
        return std::make_unique<WormholeMultichipDeviceModel>(8);
    }
    if (device_name == "blackhole" || device_name == "P100") {
        return std::make_unique<BlackholeDeviceModel>(
            BlackholeDeviceModel::DRAMHarvestingConfig::SINGLE_BANK_HARVESTING);
    }
    if (device_name == "P150") {
        return std::make_unique<BlackholeDeviceModel>(
            BlackholeDeviceModel::DRAMHarvestingConfig::NO_HARVESTING);
    }
    if (device_name == "TG") {
        return std::make_unique<WormholeMultichipDeviceModel>(36);
    }
    if (device_name == "GALAXY") {
        return std::make_unique<WormholeMultichipDeviceModel>(32);
    }
    if (device_name == "BLACKHOLE_GALAXY") {
        return std::make_unique<BlackholeMultichipDeviceModel>(32);
    }
    if (device_name == "P300") {
        return std::make_unique<BlackholeMultichipDeviceModel>(2);
    }
    if (device_name == "P150_X8") {
        return std::make_unique<BlackholeMultichipDeviceModel>(8);
    }

    log_error("Unknown device model: {}", device_name);
    throw npeException(npeErrorCode::DEVICE_MODEL_INIT_FAILED);
}

size_t npeDeviceModelFactory::getNumChips(const std::string& device_name) {
    if (device_name == "N300" || device_name == "P300") {
        return 2;
    }
    if (device_name == "T3K" || device_name == "P150_X8") {
        return 8;
    }
    if (device_name == "GALAXY" || device_name == "BLACKHOLE_GALAXY") {
        return 32;
    }
    if (device_name == "TG") {
        return 36;
    }
    return 1;
}

}  // namespace tt_npe

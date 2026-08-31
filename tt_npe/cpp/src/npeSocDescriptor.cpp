// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include "npeSocDescriptor.hpp"

#include <fstream>
#include <string>

#include <yaml-cpp/yaml.h>

#include "npeUtil.hpp"

namespace tt_npe {

namespace {

std::string coordToString(const YAML::Node& node) {
    if (node.IsScalar()) {
        return node.as<std::string>();
    }
    return "";
}

std::vector<std::string> parseCoordSequence(const YAML::Node& node) {
    std::vector<std::string> coords;
    if (!node.IsDefined() || !node.IsSequence()) {
        return coords;
    }
    for (const auto& item : node) {
        std::string coord = coordToString(item);
        if (!coord.empty()) {
            coords.push_back(coord);
        }
    }
    return coords;
}

std::vector<std::vector<std::string>> parseDramChannels(const YAML::Node& node) {
    std::vector<std::vector<std::string>> channels;
    if (!node.IsDefined() || !node.IsSequence()) {
        return channels;
    }
    for (const auto& channel : node) {
        if (channel.IsSequence()) {
            channels.push_back(parseCoordSequence(channel));
        }
    }
    return channels;
}

}  // namespace

std::optional<SocDescriptor> parseSocDescriptor(const std::string& filepath) {
    if (filepath.empty()) {
        return std::nullopt;
    }

    try {
        YAML::Node root = YAML::LoadFile(filepath);

        SocDescriptor soc;

        if (root["arch_name"]) {
            soc.arch_name = root["arch_name"].as<std::string>();
        }

        if (root["grid"]) {
            const auto& grid = root["grid"];
            if (grid["x_size"]) {
                soc.grid_x_size = grid["x_size"].as<int>();
            }
            if (grid["y_size"]) {
                soc.grid_y_size = grid["y_size"].as<int>();
            }
        }

        soc.functional_workers = parseCoordSequence(root["functional_workers"]);
        soc.dram = parseDramChannels(root["dram"]);
        soc.eth = parseCoordSequence(root["eth"]);
        soc.pcie = parseCoordSequence(root["pcie"]);
        soc.arc = parseCoordSequence(root["arc"]);
        soc.router_only = parseCoordSequence(root["router_only"]);
        soc.security = parseCoordSequence(root["security"]);
        soc.l2cpu = parseCoordSequence(root["l2cpu"]);
        soc.dispatch = parseCoordSequence(root["dispatch"]);

        if (root["worker_l1_size"]) {
            soc.worker_l1_size = root["worker_l1_size"].as<int64_t>();
        }
        if (root["dram_bank_size"]) {
            soc.dram_bank_size = root["dram_bank_size"].as<int64_t>();
        }
        if (root["eth_l1_size"]) {
            soc.eth_l1_size = root["eth_l1_size"].as<int64_t>();
        }

        soc.valid = true;
        return soc;

    } catch (const YAML::Exception& e) {
        log_warn("Failed to parse SOC descriptor file '{}': {}\n", filepath, e.what());
        return std::nullopt;
    }
}

nlohmann::json socDescriptorToJson(const SocDescriptor& soc) {
    nlohmann::ordered_json j;
    j["arch_name"] = soc.arch_name;
    j["grid"] = {{"x_size", soc.grid_x_size}, {"y_size", soc.grid_y_size}};
    j["functional_workers"] = soc.functional_workers;
    j["dram"] = soc.dram;
    j["eth"] = soc.eth;
    j["pcie"] = soc.pcie;
    j["arc"] = soc.arc;
    j["router_only"] = soc.router_only;
    if (!soc.security.empty()) {
        j["security"] = soc.security;
    }
    if (!soc.l2cpu.empty()) {
        j["l2cpu"] = soc.l2cpu;
    }
    if (!soc.dispatch.empty()) {
        j["dispatch"] = soc.dispatch;
    }
    if (soc.worker_l1_size > 0) {
        j["worker_l1_size"] = soc.worker_l1_size;
    }
    if (soc.dram_bank_size > 0) {
        j["dram_bank_size"] = soc.dram_bank_size;
    }
    if (soc.eth_l1_size > 0) {
        j["eth_l1_size"] = soc.eth_l1_size;
    }
    return j;
}

}  // namespace tt_npe

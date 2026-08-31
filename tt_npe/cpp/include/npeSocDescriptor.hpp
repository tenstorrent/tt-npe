// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace tt_npe {

struct SocDescriptor {
    std::string arch_name;
    int grid_x_size = 0;
    int grid_y_size = 0;
    std::vector<std::string> functional_workers;
    std::vector<std::vector<std::string>> dram;
    std::vector<std::string> eth;
    std::vector<std::string> pcie;
    std::vector<std::string> arc;
    std::vector<std::string> router_only;
    std::vector<std::string> security;
    std::vector<std::string> l2cpu;
    std::vector<std::string> dispatch;
    int64_t worker_l1_size = 0;
    int64_t dram_bank_size = 0;
    int64_t eth_l1_size = 0;
    bool valid = false;
};

std::optional<SocDescriptor> parseSocDescriptor(const std::string& filepath);

nlohmann::json socDescriptorToJson(const SocDescriptor& soc);

}  // namespace tt_npe

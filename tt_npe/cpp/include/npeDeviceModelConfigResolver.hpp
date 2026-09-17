// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "npeDeviceModelConfig.hpp"
#include "npeSocDescriptor.hpp"

namespace tt_npe {

struct ResolvedNpeDeviceModelConfig {
    SocDescriptor soc_descriptor;
    NpeDeviceModelConfig model_config;
    std::filesystem::path soc_descriptor_path;
    std::filesystem::path model_config_path;
};

std::string normalizeDeviceArchName(std::string_view arch_name);

std::filesystem::path resolveNpeDeviceModelConfigDirectory(
    const std::filesystem::path& explicit_directory = {});

ResolvedNpeDeviceModelConfig resolveNpeDeviceModelConfig(
    const std::filesystem::path& soc_descriptor_path,
    const std::filesystem::path& model_config_directory = {});

}  // namespace tt_npe

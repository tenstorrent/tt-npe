// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include "npeDeviceModelConfigResolver.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace tt_npe {

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

    const auto arch_name = normalizeDeviceArchName(soc_descriptor->arch_name);
    if (arch_name.empty() ||
        !std::all_of(arch_name.begin(), arch_name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        })) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "SOC descriptor '{}' has invalid arch_name '{}'",
                soc_descriptor_path.string(),
                soc_descriptor->arch_name));
    }

    const auto model_config_path = model_config_directory / (arch_name + ".yaml");
    std::error_code error;
    if (!std::filesystem::is_regular_file(model_config_path, error)) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "No NPE device model config found for architecture '{}' at '{}'",
                arch_name,
                model_config_path.string()));
    }

    return {
        .soc_descriptor = *soc_descriptor,
        .model_config = parseNpeDeviceModelConfig(model_config_path),
        .model_config_path = model_config_path};
}

}  // namespace tt_npe

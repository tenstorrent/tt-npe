// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include "npeDeviceModelConfigResolver.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

namespace tt_npe {
namespace {

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

}  // namespace

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

std::filesystem::path resolveNpeDeviceModelConfigDirectory(
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

    const auto resolved_model_config_directory =
        resolveNpeDeviceModelConfigDirectory(model_config_directory);
    const auto model_config_name =
        arch_name == "wormhole" ? std::string("wormhole_b0") : arch_name;
    const auto model_config_path =
        resolved_model_config_directory / (model_config_name + ".yaml");
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
        .soc_descriptor_path = soc_descriptor_path,
        .model_config_path = model_config_path};
}

}  // namespace tt_npe

// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "npeConfig.hpp"
#include "npeDeviceModelIface.hpp"

namespace tt_npe {

class npeDeviceModelFactory {
   public:
    static std::unique_ptr<npeDeviceModel> createDeviceModel(
        const std::string& device_name,
        const std::filesystem::path& soc_descriptor_file,
        const std::filesystem::path& model_config_directory = {});
    static std::unique_ptr<npeDeviceModel> createDeviceModel(
        const npeConfig& cfg,
        const std::filesystem::path& model_config_directory = {});
    static std::unique_ptr<npeDeviceModel> createDeviceModel(
        const std::string& device_name);

   private:
    static size_t getNumChips(const std::string& device_name);
};

}  // namespace tt_npe
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "npeCommon.hpp"
#include "npeDeviceModelConfigResolver.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeUtil.hpp"

#include "device_models/custom.hpp"
#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "device_models/blackhole.hpp"
#include "device_models/blackhole_multichip.hpp"
namespace tt_npe {

class npeDeviceModelFactory {
   public:
    static std::unique_ptr<npeDeviceModel> createDeviceModel(
        const std::string& device_name,
        const std::filesystem::path& soc_descriptor_file,
        const std::filesystem::path& model_config_directory = {}) {
        if (soc_descriptor_file.empty()) {
            return createDeviceModel(device_name);
        }

        auto resolved_config =
            resolveNpeDeviceModelConfig(soc_descriptor_file, model_config_directory);
        return std::make_unique<CustomDeviceModel>(
            std::move(resolved_config), getNumChips(device_name));
    }

    static std::unique_ptr<npeDeviceModel> createDeviceModel(const std::string& device_name) {
        if (device_name == "wormhole_b0" || device_name == "N150") {
            return std::make_unique<WormholeB0DeviceModel>();
        } else if (device_name == "N300") {
            size_t num_chips = 2;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "T3K") {
            size_t num_chips = 8;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "blackhole" || device_name == "P100") {
            return std::make_unique<BlackholeDeviceModel>(BlackholeDeviceModel::DRAMHarvestingConfig::SINGLE_BANK_HARVESTING);
        } else if (device_name == "P150") {
            return std::make_unique<BlackholeDeviceModel>(BlackholeDeviceModel::DRAMHarvestingConfig::NO_HARVESTING);
        } else if (device_name == "TG") {
            size_t num_chips = 36;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "GALAXY") {
            size_t num_chips = 32;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "BLACKHOLE_GALAXY") {
            size_t num_chips = 32;
            return std::make_unique<BlackholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "P300") {
            size_t num_chips = 2;
            return std::make_unique<BlackholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "P150_X8") {
            size_t num_chips = 8;
            return std::make_unique<BlackholeMultichipDeviceModel>(num_chips);
        } else {
            log_error("Unknown device model: {}", device_name);
            throw npeException(npeErrorCode::DEVICE_MODEL_INIT_FAILED);
        }
    }

   private:
    static size_t getNumChips(const std::string& device_name) {
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
};
}  // namespace tt_npe
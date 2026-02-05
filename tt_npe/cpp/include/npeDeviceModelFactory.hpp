// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeDeviceModelUtils.hpp"
#include "npeUtil.hpp"

#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "device_models/blackhole.hpp"

namespace tt_npe {

class npeDeviceModelFactory {
   public:
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
            return std::make_unique<BlackholeDeviceModel>(BlackholeDeviceModel::Model::p100);
        } else if (device_name == "P150") {
            return std::make_unique<BlackholeDeviceModel>(BlackholeDeviceModel::Model::p150);
        } else if (device_name == "TG") {
            size_t num_chips = 36;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "GALAXY") {
            size_t num_chips = 32;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (device_name == "BLACKHOLE_GALAXY") {
            size_t num_chips = 32;
            return std::make_unique<BlackholeMultichipDeviceModel>(num_chips);
        } else {
            log_error("Unknown device model: {}", device_name);
            throw npeException(npeErrorCode::DEVICE_MODEL_INIT_FAILED);
        }
    }
};
}  // namespace tt_npe
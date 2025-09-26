// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeUtil.hpp"

#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "device_models/blackhole.hpp"

namespace tt_npe {

class npeDeviceModelFactory {
   public:
    static std::unique_ptr<npeDeviceModel> createDeviceModel(const npeConfig& cfg) {
        if (cfg.device_name == "wormhole_b0" || cfg.device_name == "N150") {
            WormholeB0DeviceConfig device_config;
            // TODO: Parse noc_topo_override string from cfg.noc_topo_override and set device_config.noc_topo_override
            // For now, default to DEFAULT
            return std::make_unique<WormholeB0DeviceModel>(device_config);
        } else if (cfg.device_name == "N300") {
            size_t num_chips = 2;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (cfg.device_name == "T3K") {
            size_t num_chips = 8;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (cfg.device_name == "blackhole" || cfg.device_name == "P100") {
            return std::make_unique<BlackholeDeviceModel>(BlackholeDeviceModel::Model::p100);
        } else if (cfg.device_name == "P150") {
            return std::make_unique<BlackholeDeviceModel>(BlackholeDeviceModel::Model::p150);
        } else if (cfg.device_name == "TG") {
            size_t num_chips = 36;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else if (cfg.device_name == "GALAXY") {
            size_t num_chips = 32;
            return std::make_unique<WormholeMultichipDeviceModel>(num_chips);
        } else {
            log_error("Unknown device model: {}", cfg.device_name);
            throw npeException(npeErrorCode::DEVICE_MODEL_INIT_FAILED);
        }
    }
};
}  // namespace tt_npe
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <unordered_map>
#include <unordered_set>

#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"
#include "wormhole_b0.hpp"

namespace tt_npe {

class WormholeQDeviceModel : public WormholeB0DeviceModel {
   public:
    static constexpr float noc_bw_multiplier = 2.0;
    static constexpr float dram_bw_multiplier = 3.0;

    WormholeQDeviceModel() {
        // adjust bandwidth lookup according to multipliers
        for (auto& entry : tbt){
            entry.second *= noc_bw_multiplier;
        }

        core_type_to_inj_rate[CoreType::DRAM] *= dram_bw_multiplier;
        core_type_to_abs_rate[CoreType::DRAM] *= dram_bw_multiplier;

        core_type_to_inj_rate[CoreType::ETH] *= noc_bw_multiplier;
        core_type_to_abs_rate[CoreType::ETH] *= noc_bw_multiplier;
        core_type_to_inj_rate[CoreType::WORKER] *= noc_bw_multiplier;
        core_type_to_abs_rate[CoreType::WORKER] *= noc_bw_multiplier;
        core_type_to_inj_rate[CoreType::UNDEF] *= noc_bw_multiplier;
        core_type_to_abs_rate[CoreType::UNDEF] *= noc_bw_multiplier;
    }

    float getLinkBandwidth(const nocLinkID &link_id) const override { 
        return WormholeB0DeviceModel::getLinkBandwidth(link_id) * noc_bw_multiplier; 
    }
    float getAggregateDRAMBandwidth() const override { 
        return WormholeB0DeviceModel::getAggregateDRAMBandwidth() * dram_bw_multiplier; 
    }

};
}  // namespace tt_npe
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
    WormholeQDeviceModel() {
        tbt = {{0, 0}, {128, 11}, {256, 20}, {512, 36}, {1024, 60.0}, {2048, 60.0}, {8192, 60.0}};
        core_type_to_inj_rate = {
            {CoreType::DRAM, 46},
            {CoreType::ETH, 46},
            {CoreType::UNDEF, 56},
            {CoreType::WORKER, 56}};
        core_type_to_abs_rate = {
            {CoreType::DRAM, 48},
            {CoreType::ETH, 48},
            {CoreType::UNDEF, 56},
            {CoreType::WORKER, 56}};
    }

    float getLinkBandwidth(const nocLinkID &link_id) const override { return 60; }
    float getAggregateDRAMBandwidth() const override { return 512; }

};
}  // namespace tt_npe
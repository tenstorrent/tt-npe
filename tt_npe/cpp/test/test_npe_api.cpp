// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "npeAPI.hpp"
#include "npeConfig.hpp"

namespace tt_npe {

TEST(npeAPITest, CanConstructAPI) {
    npeConfig cfg;
    cfg.device_name = "wormhole_b0";
    npeAPI api(cfg);
}

TEST(npeAPITest, CanCatchUndefinedDevice) {
    npeConfig cfg;
    cfg.device_name = "undefined_af";
    EXPECT_THROW(npeAPI api(cfg), npeException);
}

TEST(npeAPITest, CanCatchInvalidConfig) {
    npeConfig cfg;
    cfg.cycles_per_timestep = 0;
    EXPECT_THROW(npeAPI api(cfg), npeException);
}

}  // namespace tt_npe
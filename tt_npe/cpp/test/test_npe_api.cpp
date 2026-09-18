// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "ingestWorkload.hpp"
#include "npeAPI.hpp"
#include "npeCommon.hpp"
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

TEST(npeAPITest, ValidatesMulticastUtilizationFromTrace) {
    auto workload =
        createWorkloadFromJSON("cpp/test/data/mcast-util-trace-small.json", "wormhole_b0", true);
    ASSERT_TRUE(workload.has_value());

    npeConfig cfg;
    cfg.device_name = "wormhole_b0";
    cfg.congestion_model_name = "fast";
    cfg.cycles_per_timestep = 32;

    npeAPI api(cfg);
    auto result = api.runNPE(*workload);
    ASSERT_TRUE(std::holds_alternative<npeStats>(result));

    const auto &stats = std::get<npeStats>(result);
    const auto &mesh_stats = stats.per_device_stats.at(MESH_DEVICE);
    ASSERT_GT(mesh_stats.overall_avg_link_util, 0.0);
    ASSERT_GT(mesh_stats.overall_avg_mcast_write_link_util, 0.0);

    constexpr double kExpectedMcastShare = 1.0;
    const double observed_mcast_share =
        mesh_stats.overall_avg_mcast_write_link_util / mesh_stats.overall_avg_link_util;
    EXPECT_NEAR(observed_mcast_share, kExpectedMcastShare, 1e-3);
}

}  // namespace tt_npe
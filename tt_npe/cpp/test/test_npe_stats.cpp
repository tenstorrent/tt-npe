// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include <filesystem>

#include "gtest/gtest.h"
#include "ingestWorkload.hpp"
#include "npeAPI.hpp"
#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "npeStats.hpp"

namespace tt_npe {
namespace {

npeConfig makeConfig() {
    npeConfig cfg;
    cfg.device_name = "wormhole_b0";
    cfg.congestion_model_name = "fast";
    cfg.cycles_per_timestep = 128;
    cfg.setVerbosityLevel(0);
    return cfg;
}

// Builds a workload where `num_senders` worker cores all write to the *same*
// destination core. The sink NIU of that destination is oversubscribed by
// roughly num_senders x, while every other NIU on the device is idle.
npeWorkload makeHotNIUWorkload(size_t num_senders) {
    npeWorkload wl;
    npeWorkloadPhase phase;
    const DeviceID device_id = 0;
    const Coord hot_dst{device_id, 9, 9};
    for (size_t i = 0; i < num_senders; i++) {
        // rows 1..N, col 1 are WORKER cores on wormhole_b0
        Coord src{device_id, int(1 + i), 1};
        phase.transfers.push_back(
            npeWorkloadTransfer(8192, 32, src, hot_dst, 28.1, 0, nocType::NOC0));
    }
    wl.addPhase(phase);
    wl.setGoldenResultCycles({{device_id, {0, 32}}});
    return wl;
}

const npeStats::deviceStats &meshStats(const npeResult &result) {
    return std::get<npeStats>(result).per_device_stats.at(MESH_DEVICE);
}

}  // namespace

// The true per-NIU/per-link peak can never be below the max of the spatial
// average, since a max over a set is always >= the mean of that set.
TEST(npeStatsTest, PeakDemandIsAtLeastMaxAvgDemand) {
    npeAPI api(makeConfig());
    auto result = api.runNPE(makeHotNIUWorkload(8));
    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    const auto &stats = meshStats(result);

    EXPECT_GE(stats.overall_peak_niu_demand, stats.overall_max_niu_demand);
    EXPECT_GE(stats.overall_peak_link_demand, stats.overall_max_link_demand);
    EXPECT_GT(stats.overall_peak_niu_demand, 0.0);
    EXPECT_GT(stats.overall_peak_link_demand, 0.0);
}

// A single hot NIU among hundreds of idle ones is completely washed out by the
// spatial average that overall_max_niu_demand is built from; overall_peak_niu_demand
// must report the hot NIU instead.
TEST(npeStatsTest, PeakNiuDemandExposesHotspotDilutedByAverage) {
    constexpr size_t kNumSenders = 8;
    npeAPI api(makeConfig());
    auto result = api.runNPE(makeHotNIUWorkload(kNumSenders));
    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    const auto &stats = meshStats(result);

    // the shared sink NIU carries ~kNumSenders concurrent transfers, so it is
    // oversubscribed well beyond 100% of a single NIU's bandwidth
    EXPECT_GT(stats.overall_peak_niu_demand, 100.0);

    // ...while the spatial average over all NIUs on the device stays tiny
    EXPECT_LT(stats.overall_max_niu_demand, 20.0);

    // the hotspot is at least an order of magnitude larger than the diluted stat
    EXPECT_GT(stats.overall_peak_niu_demand, 10.0 * stats.overall_max_niu_demand);
}

// Scaling the number of senders into a single destination scales the peak NIU
// demand, but barely moves the spatially-averaged stat.
TEST(npeStatsTest, PeakNiuDemandTracksHotspotIntensity) {
    npeAPI api(makeConfig());

    auto light = api.runNPE(makeHotNIUWorkload(2));
    auto heavy = api.runNPE(makeHotNIUWorkload(8));
    ASSERT_TRUE(std::holds_alternative<npeStats>(light));
    ASSERT_TRUE(std::holds_alternative<npeStats>(heavy));

    EXPECT_GT(meshStats(heavy).overall_peak_niu_demand,
              2.0 * meshStats(light).overall_peak_niu_demand);
}

// Regression guard on a bundled tt-metal noc trace. Skipped when the test is
// invoked from a working directory where the bundled workloads aren't visible.
TEST(npeStatsTest, PeakNiuDemandOnBundledNocTrace) {
    const std::string trace_path = "workload/noc_trace_json/2x2_BLOCK_TO_8x8_BLOCK.json";
    if (!std::filesystem::exists(trace_path)) {
        GTEST_SKIP() << "bundled noc trace not reachable from cwd; run from tt_npe/";
    }
    auto workload = createWorkloadFromJSON(trace_path, "wormhole_b0", true);
    ASSERT_TRUE(workload.has_value());

    npeAPI api(makeConfig());
    auto result = api.runNPE(*workload);
    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    const auto &stats = meshStats(result);

    // On this trace the busiest NIU is oversubscribed ~5.6x (562% of the 30
    // B/cycle link bandwidth tt-npe normalizes demand against) while the max of
    // the spatial average sits at 5.5%.
    EXPECT_GE(stats.overall_peak_niu_demand, stats.overall_max_niu_demand);
    EXPECT_GT(stats.overall_peak_niu_demand, 100.0);
    EXPECT_LT(stats.overall_max_niu_demand, 100.0);
}

}  // namespace tt_npe

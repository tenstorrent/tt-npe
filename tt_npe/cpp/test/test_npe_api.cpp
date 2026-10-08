// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <chrono>
#include <filesystem>

#include "device_models/custom.hpp"
#include "gtest/gtest.h"
#include "ingestWorkload.hpp"
#include "npeAPI.hpp"
#include "npeCommon.hpp"
#include "npeConfig.hpp"

namespace tt_npe {
namespace {

std::filesystem::path dataDirectory() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data";
}

class TemporaryProfilerTrace {
   public:
    TemporaryProfilerTrace() {
        directory_ = std::filesystem::temp_directory_path() /
                     fmt::format(
                         "tt_npe_profiler_trace_{}",
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count());
        std::filesystem::create_directories(directory_);

        trace_path_ = directory_ / "noc_trace.json";
        std::filesystem::copy_file(
            std::filesystem::path(__FILE__).parent_path() / "data" /
                "mcast-util-trace-small.json",
            trace_path_);
        std::filesystem::copy_file(
            dataDirectory() / "device/layout/arch-blackhole.yaml",
            directory_ / "soc_descriptor.yaml");
    }

    ~TemporaryProfilerTrace() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    const std::filesystem::path& path() const { return trace_path_; }

   private:
    std::filesystem::path directory_;
    std::filesystem::path trace_path_;
};

}  // namespace

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

TEST(npeAPITest, UsesSocBackedModelFromConfig) {
    npeConfig cfg;
    cfg.device_name = "quasar_prototype";
    cfg.soc_descriptor_file =
        (dataDirectory() / "device/layout/arch-blackhole.yaml").string();

    const npeAPI api(cfg);

    EXPECT_NE(
        dynamic_cast<const CustomDeviceModel*>(&api.getDeviceModel()), nullptr);
}

TEST(npeAPITest, UsesWormholeSocBackedModelFromConfig) {
    npeConfig cfg;
    cfg.device_name = "N150";
    cfg.soc_descriptor_file =
        (dataDirectory() / "device/layout/arch-wormhole.yaml").string();

    const npeAPI api(cfg);
    const auto* custom_model =
        dynamic_cast<const CustomDeviceModel*>(&api.getDeviceModel());

    ASSERT_NE(custom_model, nullptr);
    EXPECT_EQ(custom_model->getArch(), DeviceArch::WormholeB0);
}

TEST(npeAPITest, WormholeSocBackedModelMatchesHardcodedCongestion) {
    const auto workload =
        createWorkloadFromJSON(
            "cpp/test/data/mcast-util-trace-small.json",
            "wormhole_b0",
            true);
    ASSERT_TRUE(workload.has_value());

    npeConfig hardcoded_cfg;
    hardcoded_cfg.device_name = "wormhole_b0";
    hardcoded_cfg.congestion_model_name = "fast";
    hardcoded_cfg.cycles_per_timestep = 32;

    auto custom_cfg = hardcoded_cfg;
    custom_cfg.soc_descriptor_file =
        (dataDirectory() / "device/layout/arch-wormhole.yaml").string();

    const auto hardcoded_result = npeAPI(hardcoded_cfg).runNPE(*workload);
    const auto custom_result = npeAPI(custom_cfg).runNPE(*workload);
    ASSERT_TRUE(std::holds_alternative<npeStats>(hardcoded_result));
    ASSERT_TRUE(std::holds_alternative<npeStats>(custom_result));

    const auto& hardcoded_stats =
        std::get<npeStats>(hardcoded_result).per_device_stats.at(MESH_DEVICE);
    const auto& custom_stats =
        std::get<npeStats>(custom_result).per_device_stats.at(MESH_DEVICE);
    EXPECT_EQ(custom_stats.estimated_cycles, hardcoded_stats.estimated_cycles);
    EXPECT_EQ(
        custom_stats.estimated_cong_free_cycles,
        hardcoded_stats.estimated_cong_free_cycles);
    EXPECT_DOUBLE_EQ(
        custom_stats.overall_avg_link_demand,
        hardcoded_stats.overall_avg_link_demand);
    EXPECT_DOUBLE_EQ(
        custom_stats.overall_avg_link_util,
        hardcoded_stats.overall_avg_link_util);
    EXPECT_DOUBLE_EQ(
        custom_stats.overall_avg_niu_demand,
        hardcoded_stats.overall_avg_niu_demand);
    EXPECT_DOUBLE_EQ(
        custom_stats.overall_avg_mcast_write_link_util,
        hardcoded_stats.overall_avg_mcast_write_link_util);
}

TEST(npeAPITest, IngestsTraceWithSiblingSocDescriptor) {
    const TemporaryProfilerTrace trace;
    const auto workload =
        createWorkloadFromJSON(trace.path().string(), "quasar_prototype", true);

    EXPECT_TRUE(workload.has_value());
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

namespace {

npeConfig quasarMeshConfig() {
    npeConfig cfg;
    cfg.device_name = "quasar";
    cfg.congestion_model_name = "fast";
    cfg.cycles_per_timestep = 32;
    cfg.soc_descriptor_file =
        (std::filesystem::path(__FILE__).parent_path() / "data" /
         "quasar-mesh-4x3-soc-descriptor.yaml")
            .string();
    return cfg;
}

npeWorkload singleTransferWorkload(nocIndex noc) {
    npeWorkload wl;
    npeWorkloadPhase phase;
    // worker at the bottom-right corner to the worker at row 0, col 1
    phase.transfers.emplace_back(4096, 4, Coord{0, 2, 3}, Coord{0, 0, 1}, 0.0f, 0, noc);
    phase.transfers.emplace_back(
        4096, 4, Coord{0, 1, 1}, MulticastCoordSet({0, 0, 1}, {0, 2, 2}), 0.0f, 0, noc,
        "WRITE_MULTICAST");
    wl.addPhase(phase);
    wl.setGoldenResultCycles({{0, {0, 100}}});
    return wl;
}

}  // namespace

TEST(npeAPITest, RunsQuasarMeshWorkloadOnSingleNoc) {
    const npeAPI api(quasarMeshConfig());
    const auto result = api.runNPE(singleTransferWorkload(nocIndex{0}));
    ASSERT_TRUE(std::holds_alternative<npeStats>(result));

    const auto& stats = std::get<npeStats>(result).per_device_stats.at(MESH_DEVICE);
    EXPECT_GT(stats.estimated_cycles, 0);
    EXPECT_GT(stats.overall_per_noc[0].avg_link_util, 0.0);
    EXPECT_DOUBLE_EQ(stats.overall_per_noc[1].avg_link_util, 0.0);
    EXPECT_DOUBLE_EQ(stats.overall_per_noc[1].avg_link_demand, 0.0);
    EXPECT_DOUBLE_EQ(stats.overall_per_noc[0].avg_link_util, stats.overall_avg_link_util);
}

TEST(npeAPITest, QuasarMeshStatsNormalizeByInGridLinks) {
    npeWorkload wl;
    npeWorkloadPhase phase;
    // 4-hop X-then-Y route: west twice, then north twice
    phase.transfers.emplace_back(4096, 4, Coord{0, 2, 3}, Coord{0, 0, 1}, 0.0f, 0, nocIndex{0});
    wl.addPhase(phase);
    wl.setGoldenResultCycles({{0, {0, 100}}});

    const npeAPI api(quasarMeshConfig());
    const auto result = api.runNPE(wl);
    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    const auto& timesteps = std::get<npeStats>(result).per_device_stats.at(MESH_DEVICE).per_timestep_stats;
    ASSERT_FALSE(timesteps.empty());
    const auto& first = timesteps.front();

    // quasar.yaml: 4096B packets interpolate to 192.0 B/cycle steady state;
    // with 4 packets, 1/4 of the transfer runs at the 243.6 B/cycle peak.
    constexpr double link_bandwidth = 243.6;
    constexpr double transfer_bandwidth = 0.75 * 192.0 + 0.25 * 243.6;
    constexpr double route_links = 4;
    // 4x3 mesh: 2*3*(4-1) east/west links + 2*4*(3-1) north/south links
    constexpr double in_grid_links = 34;
    constexpr double nius = 4 * 3 * 2;

    const double expected_avg_link_demand =
        100.0 * route_links * transfer_bandwidth / (link_bandwidth * in_grid_links);
    EXPECT_NEAR(first.avg_link_demand, expected_avg_link_demand, 1e-3);
    EXPECT_NEAR(first.per_noc[0].avg_link_demand, expected_avg_link_demand, 1e-3);
    EXPECT_NEAR(first.per_noc[0].avg_link_util, expected_avg_link_demand, 1e-3);
    EXPECT_NEAR(first.per_noc[0].max_link_demand, 100.0 * transfer_bandwidth / link_bandwidth, 1e-3);
    EXPECT_DOUBLE_EQ(first.per_noc[1].avg_link_demand, 0.0);
    EXPECT_DOUBLE_EQ(first.per_noc[1].max_link_demand, 0.0);
    EXPECT_NEAR(
        first.avg_niu_demand, 100.0 * 2 * transfer_bandwidth / (link_bandwidth * nius), 1e-3);
}

TEST(npeAPITest, RejectsNoc1TransferOnSingleNocDevice) {
    const npeAPI api(quasarMeshConfig());
    const auto result = api.runNPE(singleTransferWorkload(nocIndex{1}));

    ASSERT_TRUE(std::holds_alternative<npeException>(result));
    EXPECT_EQ(
        std::get<npeException>(result).err_code, npeErrorCode::WORKLOAD_VALIDATION_FAILED);
}

}  // namespace tt_npe
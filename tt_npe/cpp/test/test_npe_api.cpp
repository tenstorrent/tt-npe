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

}  // namespace tt_npe
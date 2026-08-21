// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include <array>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "fmt/format.h"
#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "npeDeviceModelUtils.hpp"
#include "npeEngine.hpp"
#include "npeStats.hpp"
#include "npeTimelineJsonWriter.hpp"
#include "npeTimelineUtil.hpp"
#include "zstd.h"

namespace tt_npe {
namespace {

template <typename T>
concept RetainsLiveTransferIDs = requires(T value) {
    value.live_transfer_ids;
};

template <typename T>
concept MaterializesActiveTransferVector = requires(T value) {
    value.activeTransfers(0, 1);
};

template <typename T>
concept RetainsDenseLinkDemandGrid = requires(T value) {
    value.link_demand_grid;
};

template <typename T>
concept RetainsDenseNIUDemandGrid = requires(T value) {
    value.niu_demand_grid;
};

static_assert(!RetainsLiveTransferIDs<TimestepStats>);
static_assert(!MaterializesActiveTransferVector<TimelineActiveTransferSweep>);
static_assert(!RetainsDenseLinkDemandGrid<TimestepStats>);
static_assert(!RetainsDenseNIUDemandGrid<TimestepStats>);
static_assert(sizeof(SparseDemandEntry) <= 8);

std::vector<int> collectActiveTransfers(
    TimelineActiveTransferSweep& sweep,
    Cycle timestep_start,
    Cycle timestep_end) {
    sweep.update(timestep_start, timestep_end);
    std::vector<int> active_transfers;
    sweep.forEachActiveTransfer(
        [&](int logical_id) { active_transfers.push_back(logical_id); });
    return active_transfers;
}

npeWorkload makeTimelineWorkload() {
    npeWorkload workload;
    auto transfer_group_id = workload.registerTransferGroupID();
    npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048,
        1,
        {0, 1, 1},
        Coord{0, 1, 5},
        28.1,
        0,
        nocType::NOC1,
        "READ",
        "",
        transfer_group_id,
        0));
    phase.transfers.push_back(npeWorkloadTransfer(
        2048,
        1,
        {0, 2, 1},
        Coord{0, 2, 5},
        28.1,
        0,
        nocType::NOC1,
        "READ",
        "",
        transfer_group_id,
        1));
    workload.addPhase(phase);
    workload.setGoldenResultCycles({{0, {0, 32}}});
    return workload;
}

npeWorkload makeMultichipTimelineWorkload() {
    auto workload = makeTimelineWorkload();
    boost::unordered_flat_map<DeviceID, std::pair<Cycle, Cycle>> golden_cycles;
    for (DeviceID device_id = 0; device_id < 2; ++device_id) {
        golden_cycles[device_id] = {0, 32};
    }
    workload.setGoldenResultCycles(std::move(golden_cycles));
    return workload;
}

nlohmann::ordered_json readJson(const std::filesystem::path& filepath) {
    std::ifstream input(filepath);
    return nlohmann::ordered_json::parse(input);
}

std::string readFile(const std::filesystem::path& filepath) {
    std::ifstream input(filepath, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

std::string readCompressedFile(const std::filesystem::path& filepath) {
    auto compressed = readFile(filepath);
    ZSTD_DStream* stream = ZSTD_createDStream();
    if (stream == nullptr || ZSTD_isError(ZSTD_initDStream(stream))) {
        ZSTD_freeDStream(stream);
        return {};
    }

    std::string decompressed;
    std::vector<char> output_buffer(ZSTD_DStreamOutSize());
    ZSTD_inBuffer input{compressed.data(), compressed.size(), 0};
    while (input.pos < input.size) {
        ZSTD_outBuffer output{output_buffer.data(), output_buffer.size(), 0};
        size_t result = ZSTD_decompressStream(stream, &output, &input);
        if (ZSTD_isError(result)) {
            ZSTD_freeDStream(stream);
            return "";
        }
        decompressed.append(output_buffer.data(), output.pos);
    }
    ZSTD_freeDStream(stream);
    return decompressed;
}

nlohmann::ordered_json readCompressedJson(const std::filesystem::path& filepath) {
    return nlohmann::ordered_json::parse(readCompressedFile(filepath));
}

std::vector<std::string> objectKeys(const nlohmann::ordered_json& object) {
    std::vector<std::string> keys;
    for (const auto& item : object.items()) {
        keys.push_back(item.key());
    }
    return keys;
}

class TimelineSerializationTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir =
            std::filesystem::temp_directory_path() / "npe_timeline_serialization_test";
        std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
};

TEST_F(TimelineSerializationTest, WritesIntegralArrayItemsAsExactDecimalJson) {
    auto filepath = temp_dir / "integral-items.json.zst";
    TimelineJsonWriter writer(filepath.string(), true);

    ASSERT_TRUE(writer.beginArrayField("ids"));
    ASSERT_TRUE(writer.writeArrayItem(std::numeric_limits<int64_t>::min()));
    ASSERT_TRUE(writer.writeArrayItem(int64_t{0}));
    ASSERT_TRUE(writer.writeArrayItem(std::numeric_limits<int64_t>::max()));
    ASSERT_TRUE(writer.writeArrayItem(std::numeric_limits<uint64_t>::max()));
    ASSERT_TRUE(writer.writeArrayItem(true));
    ASSERT_TRUE(writer.endArray());
    ASSERT_TRUE(writer.close());

    EXPECT_EQ(
        readCompressedFile(filepath),
        "{\"ids\":[-9223372036854775808,0,9223372036854775807,"
        "18446744073709551615,true]}");
}

void expectV1TimelineShape(const nlohmann::ordered_json& timeline) {
    EXPECT_EQ(
        objectKeys(timeline),
        (std::vector<std::string>{
            "common_info", "chips", "noc_transfers", "zones", "timestep_data"}));
    EXPECT_EQ(
        objectKeys(timeline["common_info"]),
        (std::vector<std::string>{
            "version",
            "mesh_device",
            "arch",
            "cycles_per_timestep",
            "congestion_model_name",
            "num_rows",
            "num_cols",
            "dram_bw_util",
            "link_util",
            "mcast_write_link_util",
            "link_demand",
            "max_link_demand",
            "noc"}));
    EXPECT_EQ(timeline["common_info"]["version"], "1.0.0");
    EXPECT_EQ(timeline["common_info"]["mesh_device"], "wormhole_b0");
    EXPECT_EQ(timeline["common_info"]["arch"], "wormhole_b0");
    EXPECT_EQ(timeline["common_info"]["cycles_per_timestep"], 128);
    EXPECT_EQ(timeline["common_info"]["num_rows"], 12);
    EXPECT_EQ(timeline["common_info"]["num_cols"], 10);

    ASSERT_EQ(timeline["noc_transfers"].size(), 1);
    const auto& transfer = timeline["noc_transfers"][0];
    EXPECT_EQ(
        objectKeys(transfer),
        (std::vector<std::string>{
            "id",
            "src",
            "total_bytes",
            "start_cycle",
            "noc_event_type",
            "fabric_event_type",
            "zones",
            "end_cycle",
            "dst",
            "route"}));
    EXPECT_EQ(transfer["id"], 0);
    EXPECT_EQ(transfer["src"], nlohmann::ordered_json::array({0, 1, 1}));
    EXPECT_EQ(transfer["dst"], nlohmann::ordered_json::array({{0, 2, 5}}));
    EXPECT_EQ(transfer["total_bytes"], 2048);
    EXPECT_EQ(transfer["noc_event_type"], "READ");
    EXPECT_EQ(transfer["route"].size(), 2);

    ASSERT_FALSE(timeline["timestep_data"].empty());
    const auto& timestep = timeline["timestep_data"][0];
    EXPECT_EQ(
        objectKeys(timestep),
        (std::vector<std::string>{
            "start_cycle",
            "end_cycle",
            "active_transfers",
            "link_demand",
            "avg_link_demand",
            "avg_link_util",
            "mcast_write_link_util",
            "noc"}));
    EXPECT_EQ(timestep["start_cycle"], 0);
    EXPECT_EQ(timestep["end_cycle"], 128);
    EXPECT_EQ(timestep["active_transfers"], nlohmann::ordered_json::array({0}));
}

TEST(TimelineActiveTransferSweepTest, UsesSnapshotBoundarySemantics) {
    TimelineActiveTransferSweep sweep({
        {.component_id = 10, .logical_id = 10, .start_cycle = 5, .end_cycle = 20},
        {.component_id = 11, .logical_id = 11, .start_cycle = 20, .end_cycle = 30},
        {.component_id = 12, .logical_id = 12, .start_cycle = 21, .end_cycle = 40},
    });

    EXPECT_EQ(collectActiveTransfers(sweep, 0, 10), (std::vector<int>{10}));
    EXPECT_EQ(collectActiveTransfers(sweep, 10, 20), (std::vector<int>{10, 11}));
    EXPECT_EQ(collectActiveTransfers(sweep, 20, 30), (std::vector<int>{11, 12}));
    EXPECT_EQ(collectActiveTransfers(sweep, 30, 40), (std::vector<int>{12}));
}

TEST(TimelineActiveTransferSweepTest, RefcountsOverlappingComponentsInGroup) {
    TimelineActiveTransferSweep sweep({
        {.component_id = 0, .logical_id = 7, .start_cycle = 0, .end_cycle = 25},
        {.component_id = 1, .logical_id = 7, .start_cycle = 10, .end_cycle = 40},
        {.component_id = 2, .logical_id = 8, .start_cycle = 15, .end_cycle = 20},
    });

    EXPECT_EQ(collectActiveTransfers(sweep, 0, 10), (std::vector<int>{7}));
    EXPECT_EQ(collectActiveTransfers(sweep, 10, 20), (std::vector<int>{7, 8}));
    EXPECT_EQ(collectActiveTransfers(sweep, 20, 30), (std::vector<int>{7}));
    EXPECT_EQ(collectActiveTransfers(sweep, 30, 40), (std::vector<int>{7}));
    EXPECT_TRUE(collectActiveTransfers(sweep, 40, 50).empty());
}

TEST(TimelineActiveTransferSweepTest, PreservesMembershipAfterTimestepRegression) {
    TimelineActiveTransferSweep sweep({
        {.component_id = 0, .logical_id = 0, .start_cycle = 50, .end_cycle = 100},
    });

    EXPECT_EQ(collectActiveTransfers(sweep, 0, 100), (std::vector<int>{0}));
    EXPECT_TRUE(collectActiveTransfers(sweep, 0, 0).empty());
}

TEST(TimelineActiveTransferSweepTest, InitializesAtNonzeroSplitStart) {
    TimelineActiveTransferSweep sweep({
        {.component_id = 0, .logical_id = 0, .start_cycle = 0, .end_cycle = 50},
        {.component_id = 1, .logical_id = 1, .start_cycle = 25, .end_cycle = 150},
        {.component_id = 2, .logical_id = 2, .start_cycle = 105, .end_cycle = 120},
    });

    EXPECT_EQ(
        collectActiveTransfers(sweep, 100, 110), (std::vector<int>{1, 2}));
}

TEST(TimelineActiveTransferSweepTest, IteratesLargeActiveSetWithoutMaterializing) {
    constexpr int num_active_transfers = 20000;
    constexpr int num_timesteps = 128;
    std::vector<TimelineTransferInterval> intervals;
    intervals.reserve(num_active_transfers);
    for (int id = 0; id < num_active_transfers; ++id) {
        intervals.push_back(
            {.component_id = id,
             .logical_id = id,
             .start_cycle = 0,
             .end_cycle = num_timesteps + 1});
    }
    TimelineActiveTransferSweep sweep(std::move(intervals));

    for (int timestep = 0; timestep < num_timesteps; ++timestep) {
        sweep.update(timestep, timestep + 1);
        size_t count = 0;
        int previous_id = -1;
        sweep.forEachActiveTransfer([&](int logical_id) {
            EXPECT_GT(logical_id, previous_id);
            previous_id = logical_id;
            ++count;
        });
        EXPECT_EQ(count, num_active_transfers);
    }
}

TEST(TimelineTimestepRangeTest, VisitsEachTimestepOnceAcrossManySplits) {
    constexpr size_t timestep_count = 10003;
    constexpr size_t split_size = 7;
    std::vector<size_t> timesteps(timestep_count);
    std::iota(timesteps.begin(), timesteps.end(), 0);
    size_t visits = 0;

    for (size_t start = 0; start < timestep_count; start += split_size) {
        size_t end = std::min(start + split_size, timestep_count);
        for (size_t timestep :
             timelineTimestepSubrange(
                 timesteps, TimelineTimestepRange{start, end})) {
            EXPECT_EQ(timestep, visits);
            ++visits;
        }
    }

    EXPECT_EQ(visits, timestep_count);
}

void referenceUpdateSimulationStats(
    const npeDeviceModel& model,
    const LinkDemandGrid& link_demands,
    const LinkDemandGrid& multicast_demands,
    const NIUDemandGrid& niu_demands,
    npeStats& stats,
    bool retain_timeline_demands) {
    float bandwidth = model.getLinkBandwidth(nocLinkID(0));
    for (auto& [device_id, device_stats] : stats.per_device_stats) {
        if (device_stats.per_timestep_stats.empty()) {
            continue;
        }
        auto& timestep = device_stats.per_timestep_stats.back();
        for (const auto& [link_id, demand] : enumerate(link_demands)) {
            auto attr = model.getLinkAttributes(link_id);
            if (device_id != MESH_DEVICE && device_id != attr.coord.device_id) {
                continue;
            }
            if (device_id == MESH_DEVICE && retain_timeline_demands &&
                demand > TIMELINE_DEMAND_SIGNIFICANCE_THRESHOLD) {
                timestep.significant_link_demands.push_back(
                    {static_cast<uint32_t>(link_id), demand});
            }
            auto util = std::fmin(demand, bandwidth);
            timestep.avg_link_demand += demand;
            timestep.avg_link_util += util;
            timestep.avg_mcast_write_link_util +=
                std::fmin(multicast_demands[link_id], bandwidth);
            timestep.max_link_demand =
                std::fmax(timestep.max_link_demand, demand);
            if (attr.type == nocLinkType::NOC0_EAST ||
                attr.type == nocLinkType::NOC0_SOUTH) {
                timestep.avg_noc0_link_demand += demand;
                timestep.avg_noc0_link_util += util;
                timestep.max_noc0_link_demand =
                    std::fmax(timestep.max_noc0_link_demand, demand);
            } else {
                timestep.avg_noc1_link_demand += demand;
                timestep.avg_noc1_link_util += util;
                timestep.max_noc1_link_demand =
                    std::fmax(timestep.max_noc1_link_demand, demand);
            }
        }

        size_t link_count = device_id == MESH_DEVICE
            ? link_demands.size()
            : link_demands.size() / model.getNumChips();
        timestep.avg_link_demand *= 100.0 / (bandwidth * link_count);
        timestep.avg_link_util *= 100.0 / (bandwidth * link_count);
        timestep.avg_mcast_write_link_util *=
            100.0 / (bandwidth * link_count);
        timestep.max_link_demand *= 100.0 / bandwidth;
        size_t noc_link_count = link_count / 2;
        timestep.avg_noc0_link_demand *=
            100.0 / (bandwidth * noc_link_count);
        timestep.avg_noc0_link_util *=
            100.0 / (bandwidth * noc_link_count);
        timestep.max_noc0_link_demand *= 100.0 / bandwidth;
        timestep.avg_noc1_link_demand *=
            100.0 / (bandwidth * noc_link_count);
        timestep.avg_noc1_link_util *=
            100.0 / (bandwidth * noc_link_count);
        timestep.max_noc1_link_demand *= 100.0 / bandwidth;

        for (const auto& [niu_id, demand] : enumerate(niu_demands)) {
            auto attr = model.getNIUAttributes(niu_id);
            if (device_id != MESH_DEVICE && device_id != attr.coord.device_id) {
                continue;
            }
            if (device_id == MESH_DEVICE && retain_timeline_demands &&
                demand > TIMELINE_DEMAND_SIGNIFICANCE_THRESHOLD) {
                timestep.significant_niu_demands.push_back(
                    {static_cast<uint32_t>(niu_id), demand});
            }
            timestep.avg_niu_demand += demand;
            timestep.max_niu_demand =
                std::fmax(timestep.max_niu_demand, demand);
        }
        timestep.avg_niu_demand *=
            100.0 / (bandwidth * niu_demands.size());
        timestep.max_niu_demand *= 100.0 / bandwidth;
    }
}

void expectTimestepStatsEqual(
    const TimestepStats& actual,
    const TimestepStats& expected) {
    EXPECT_DOUBLE_EQ(actual.avg_link_demand, expected.avg_link_demand);
    EXPECT_DOUBLE_EQ(actual.max_link_demand, expected.max_link_demand);
    EXPECT_DOUBLE_EQ(actual.avg_link_util, expected.avg_link_util);
    EXPECT_DOUBLE_EQ(actual.avg_niu_demand, expected.avg_niu_demand);
    EXPECT_DOUBLE_EQ(actual.max_niu_demand, expected.max_niu_demand);
    EXPECT_DOUBLE_EQ(
        actual.avg_noc0_link_demand, expected.avg_noc0_link_demand);
    EXPECT_DOUBLE_EQ(actual.avg_noc0_link_util, expected.avg_noc0_link_util);
    EXPECT_DOUBLE_EQ(
        actual.max_noc0_link_demand, expected.max_noc0_link_demand);
    EXPECT_DOUBLE_EQ(
        actual.avg_noc1_link_demand, expected.avg_noc1_link_demand);
    EXPECT_DOUBLE_EQ(actual.avg_noc1_link_util, expected.avg_noc1_link_util);
    EXPECT_DOUBLE_EQ(
        actual.max_noc1_link_demand, expected.max_noc1_link_demand);
    EXPECT_DOUBLE_EQ(
        actual.avg_mcast_write_link_util,
        expected.avg_mcast_write_link_util);
    ASSERT_EQ(
        actual.significant_link_demands.size(),
        expected.significant_link_demands.size());
    for (size_t i = 0; i < actual.significant_link_demands.size(); ++i) {
        EXPECT_EQ(
            actual.significant_link_demands[i].id,
            expected.significant_link_demands[i].id);
        EXPECT_FLOAT_EQ(
            actual.significant_link_demands[i].demand,
            expected.significant_link_demands[i].demand);
    }
    ASSERT_EQ(
        actual.significant_niu_demands.size(),
        expected.significant_niu_demands.size());
    for (size_t i = 0; i < actual.significant_niu_demands.size(); ++i) {
        EXPECT_EQ(
            actual.significant_niu_demands[i].id,
            expected.significant_niu_demands[i].id);
        EXPECT_FLOAT_EQ(
            actual.significant_niu_demands[i].demand,
            expected.significant_niu_demands[i].demand);
    }
}

void expectDeviceSummaryMatchesReference(
    const npeStats::deviceStats& actual,
    const std::vector<TimestepStats>& reference_timesteps,
    size_t summary_timestep_count,
    Cycle estimated_cycles,
    Cycle golden_cycles) {
    TimestepStats sums;
    double max_link_util = 0;
    for (const auto& timestep : reference_timesteps) {
        sums.avg_link_demand += timestep.avg_link_demand;
        sums.max_link_demand =
            std::max(sums.max_link_demand, timestep.avg_link_demand);
        sums.avg_link_util += timestep.avg_link_util;
        max_link_util = std::max(max_link_util, timestep.avg_link_util);
        sums.avg_niu_demand += timestep.avg_niu_demand;
        sums.max_niu_demand =
            std::max(sums.max_niu_demand, timestep.avg_niu_demand);
        sums.avg_noc0_link_demand += timestep.avg_noc0_link_demand;
        sums.avg_noc0_link_util += timestep.avg_noc0_link_util;
        sums.max_noc0_link_demand = std::max(
            sums.max_noc0_link_demand, timestep.avg_noc0_link_demand);
        sums.avg_noc1_link_demand += timestep.avg_noc1_link_demand;
        sums.avg_noc1_link_util += timestep.avg_noc1_link_util;
        sums.max_noc1_link_demand = std::max(
            sums.max_noc1_link_demand, timestep.avg_noc1_link_demand);
        sums.avg_mcast_write_link_util +=
            timestep.avg_mcast_write_link_util;
    }

    EXPECT_DOUBLE_EQ(
        actual.overall_avg_link_demand,
        sums.avg_link_demand / summary_timestep_count);
    EXPECT_DOUBLE_EQ(
        actual.overall_max_link_demand, sums.max_link_demand);
    EXPECT_DOUBLE_EQ(
        actual.overall_avg_link_util,
        sums.avg_link_util / summary_timestep_count);
    EXPECT_DOUBLE_EQ(actual.overall_max_link_util, max_link_util);
    EXPECT_DOUBLE_EQ(
        actual.overall_avg_niu_demand,
        sums.avg_niu_demand / summary_timestep_count);
    EXPECT_DOUBLE_EQ(actual.overall_max_niu_demand, sums.max_niu_demand);
    EXPECT_DOUBLE_EQ(
        actual.overall_avg_noc0_link_demand,
        sums.avg_noc0_link_demand / summary_timestep_count);
    EXPECT_DOUBLE_EQ(
        actual.overall_avg_noc0_link_util,
        sums.avg_noc0_link_util / summary_timestep_count);
    EXPECT_DOUBLE_EQ(
        actual.overall_max_noc0_link_demand,
        sums.max_noc0_link_demand);
    EXPECT_DOUBLE_EQ(
        actual.overall_avg_noc1_link_demand,
        sums.avg_noc1_link_demand / summary_timestep_count);
    EXPECT_DOUBLE_EQ(
        actual.overall_avg_noc1_link_util,
        sums.avg_noc1_link_util / summary_timestep_count);
    EXPECT_DOUBLE_EQ(
        actual.overall_max_noc1_link_demand,
        sums.max_noc1_link_demand);
    EXPECT_DOUBLE_EQ(
        actual.overall_avg_mcast_write_link_util,
        sums.avg_mcast_write_link_util / summary_timestep_count);
    EXPECT_EQ(actual.estimated_cycles, estimated_cycles);
    EXPECT_EQ(actual.golden_cycles, golden_cycles);
    EXPECT_DOUBLE_EQ(
        actual.cycle_prediction_error,
        100.0 *
            float(int64_t(estimated_cycles) - int64_t(golden_cycles)) /
            golden_cycles);
    EXPECT_DOUBLE_EQ(actual.dram_bw_util, 0);
    EXPECT_DOUBLE_EQ(actual.dram_bw_util_sim, 0);
    EXPECT_TRUE(actual.eth_bw_util_per_core.empty());
}

TEST(TimelineStatsTest, MatchesMultichipReferenceAggregation) {
    WormholeMultichipDeviceModel model(3);
    auto device_state = model.initDeviceState();
    auto& link_demands = device_state->getLinkDemandGrid();
    auto& multicast_demands =
        device_state->getMulticastWriteLinkDemandGrid();
    auto& niu_demands = device_state->getNIUDemandGrid();
    for (size_t id = 0; id < link_demands.size(); ++id) {
        link_demands[id] = static_cast<float>((id % 11) + 1) * 0.00025f;
        multicast_demands[id] =
            static_cast<float>((id % 7) + 1) * 0.00015f;
    }
    for (size_t id = 0; id < niu_demands.size(); ++id) {
        niu_demands[id] = static_cast<float>((id % 9) + 1) * 0.0002f;
    }

    npeStats expected(&model);
    npeStats actual(&model);
    constexpr std::array<DeviceID, 3> active_devices{
        static_cast<DeviceID>(MESH_DEVICE), DeviceID{0}, DeviceID{1}};
    for (DeviceID id : active_devices) {
        expected.per_device_stats[id].per_timestep_stats.push_back({});
        actual.per_device_stats[id].per_timestep_stats.push_back({});
    }
    referenceUpdateSimulationStats(
        model,
        link_demands,
        multicast_demands,
        niu_demands,
        expected,
        true);
    updateSimulationStats(
        model,
        link_demands,
        multicast_demands,
        niu_demands,
        actual,
        true);

    for (DeviceID id : active_devices) {
        expectTimestepStatsEqual(
            actual.per_device_stats.at(id).per_timestep_stats.back(),
            expected.per_device_stats.at(id).per_timestep_stats.back());
    }
    EXPECT_TRUE(actual.per_device_stats.at(2).per_timestep_stats.empty());
}

TEST(TimelineStatsTest, OnlineSummariesMatchMultichipReference) {
    WormholeMultichipDeviceModel model(3);
    auto device_state = model.initDeviceState();
    auto& link_demands = device_state->getLinkDemandGrid();
    auto& multicast_demands =
        device_state->getMulticastWriteLinkDemandGrid();
    auto& niu_demands = device_state->getNIUDemandGrid();
    npeWorkload workload;
    constexpr Cycle golden_end = 2000;
    workload.setGoldenResultCycles(
        {{static_cast<DeviceID>(MESH_DEVICE), {0, golden_end}},
         {0, {0, golden_end}},
         {1, {0, golden_end}},
         {2, {0, golden_end}}});
    constexpr std::array<DeviceID, 3> active_devices{
        static_cast<DeviceID>(MESH_DEVICE), DeviceID{0}, DeviceID{1}};
    npeStats reference(&model);
    npeStats actual(&model);

    constexpr size_t simulated_timesteps = 9;
    for (size_t timestep = 0; timestep < simulated_timesteps; ++timestep) {
        for (size_t id = 0; id < link_demands.size(); ++id) {
            link_demands[id] =
                static_cast<float>((id + timestep) % 11) * 0.03f;
            multicast_demands[id] =
                static_cast<float>((id + timestep) % 7) * 0.02f;
        }
        for (size_t id = 0; id < niu_demands.size(); ++id) {
            niu_demands[id] =
                static_cast<float>((id + timestep) % 9) * 0.025f;
        }
        Cycle start = timestep * 128;
        actual.insertTimestep(start, start + 128, workload, false);
        for (DeviceID device_id : active_devices) {
            reference.per_device_stats[device_id]
                .per_timestep_stats.push_back({});
        }
        referenceUpdateSimulationStats(
            model,
            link_demands,
            multicast_demands,
            niu_demands,
            reference,
            false);
        updateSimulationStats(
            model,
            link_demands,
            multicast_demands,
            niu_demands,
            actual,
            false);
    }

    constexpr Cycle final_transfer_end = simulated_timesteps * 128;
    for (DeviceID device_id : active_devices) {
        actual.updateWorstCaseTransferEndCycle(
            device_id, 0, final_transfer_end, {0, golden_end});
    }
    actual.finishSimulation(1, 128, workload);
    actual.computeSummaryStats(workload);

    constexpr size_t summary_timestep_count = simulated_timesteps + 1;
    for (DeviceID device_id : active_devices) {
        expectDeviceSummaryMatchesReference(
            actual.per_device_stats.at(device_id),
            reference.per_device_stats.at(device_id).per_timestep_stats,
            summary_timestep_count,
            final_transfer_end,
            golden_end);
        EXPECT_TRUE(
            actual.per_device_stats.at(device_id)
                .per_timestep_stats.empty());
    }
    EXPECT_EQ(actual.per_device_stats.at(2).estimated_cycles, 0);
    EXPECT_EQ(actual.per_device_stats.at(2).overall_avg_link_demand, 0);
}

TEST(TimelineStatsTest, RetainedTimestepsScaleOnlyWithMeshTimelineDetail) {
    WormholeMultichipDeviceModel model(3);
    npeWorkload workload;
    workload.setGoldenResultCycles(
        {{static_cast<DeviceID>(MESH_DEVICE), {0, 200000}},
         {0, {0, 200000}},
         {1, {0, 200000}},
         {2, {0, 200000}}});
    constexpr size_t timestep_count = 1000;

    npeStats timeline_stats(&model);
    npeStats summary_only_stats(&model);
    for (size_t timestep = 0; timestep < timestep_count; ++timestep) {
        Cycle start = timestep * 128;
        timeline_stats.insertTimestep(start, start + 128, workload, true);
        summary_only_stats.insertTimestep(start, start + 128, workload, false);
    }

    EXPECT_EQ(
        timeline_stats.per_device_stats.at(MESH_DEVICE)
            .per_timestep_stats.size(),
        timestep_count);
    EXPECT_TRUE(
        summary_only_stats.per_device_stats.at(MESH_DEVICE)
            .per_timestep_stats.empty());
    for (DeviceID device_id : model.getDeviceIDs()) {
        EXPECT_TRUE(
            timeline_stats.per_device_stats.at(device_id)
                .per_timestep_stats.empty());
        EXPECT_TRUE(
            summary_only_stats.per_device_stats.at(device_id)
                .per_timestep_stats.empty());
    }
}

TEST(TimelineStatsTest, RetainsOnlySignificantDemandsInAscendingIDOrder) {
    WormholeB0DeviceModel model;
    auto device_state = model.initDeviceState();
    auto& link_demands = device_state->getLinkDemandGrid();
    auto& niu_demands = device_state->getNIUDemandGrid();
    link_demands[1] = 0.0009f;
    link_demands[2] = 0.001f;
    link_demands[3] = 0.5f;
    link_demands[5] = 0.25f;
    niu_demands[1] = 0.0009f;
    niu_demands[2] = 0.001f;
    niu_demands[4] = 0.75f;
    niu_demands[6] = 0.125f;

    npeStats timeline_stats(&model);
    timeline_stats.per_device_stats[MESH_DEVICE].per_timestep_stats.push_back({});
    updateSimulationStats(
        model,
        device_state->getLinkDemandGrid(),
        device_state->getMulticastWriteLinkDemandGrid(),
        device_state->getNIUDemandGrid(),
        timeline_stats,
        true);
    const auto& timeline_timestep =
        timeline_stats.per_device_stats[MESH_DEVICE].per_timestep_stats.back();
    ASSERT_EQ(timeline_timestep.significant_niu_demands.size(), 2);
    EXPECT_EQ(timeline_timestep.significant_niu_demands[0].id, 4);
    EXPECT_FLOAT_EQ(timeline_timestep.significant_niu_demands[0].demand, 0.75f);
    EXPECT_EQ(timeline_timestep.significant_niu_demands[1].id, 6);
    EXPECT_FLOAT_EQ(timeline_timestep.significant_niu_demands[1].demand, 0.125f);
    ASSERT_EQ(timeline_timestep.significant_link_demands.size(), 2);
    EXPECT_EQ(timeline_timestep.significant_link_demands[0].id, 3);
    EXPECT_FLOAT_EQ(timeline_timestep.significant_link_demands[0].demand, 0.5f);
    EXPECT_EQ(timeline_timestep.significant_link_demands[1].id, 5);
    EXPECT_FLOAT_EQ(timeline_timestep.significant_link_demands[1].demand, 0.25f);

    constexpr double expected_link_sum = 0.0009 + 0.001 + 0.5 + 0.25;
    constexpr double expected_niu_sum = 0.0009 + 0.001 + 0.75 + 0.125;
    EXPECT_NEAR(
        timeline_timestep.avg_link_demand,
        expected_link_sum * 100.0 /
            (model.getLinkBandwidth(nocLinkID(0)) * link_demands.size()),
        1e-8);
    EXPECT_NEAR(
        timeline_timestep.avg_niu_demand,
        expected_niu_sum * 100.0 /
            (model.getLinkBandwidth(nocLinkID(0)) * niu_demands.size()),
        1e-8);
}

TEST(TimelineStatsTest, OmitsSparseDemandsWhenTimelineOutputIsDisabled) {
    WormholeB0DeviceModel model;
    auto device_state = model.initDeviceState();
    device_state->getLinkDemandGrid()[0] = 1.0f;
    device_state->getNIUDemandGrid()[0] = 1.0f;

    npeStats stats(&model);
    stats.per_device_stats[MESH_DEVICE].per_timestep_stats.push_back({});
    updateSimulationStats(
        model,
        device_state->getLinkDemandGrid(),
        device_state->getMulticastWriteLinkDemandGrid(),
        device_state->getNIUDemandGrid(),
        stats,
        false);
    const auto& timestep =
        stats.per_device_stats[MESH_DEVICE].per_timestep_stats.back();
    EXPECT_GT(timestep.avg_link_demand, 0);
    EXPECT_GT(timestep.avg_niu_demand, 0);
    EXPECT_TRUE(timestep.significant_link_demands.empty());
    EXPECT_TRUE(timestep.significant_niu_demands.empty());
}

TEST_F(TimelineSerializationTest, WritesLegacyFullSchemaAndActiveComponentIDs) {
    auto filepath = temp_dir / "timeline.npeviz";

    npeConfig config;
    config.emit_timeline_file = true;
    config.use_legacy_timeline_format = true;
    config.timeline_filepath = filepath.string();
    npeEngine engine("wormhole_b0");
    auto result = engine.runPerfEstimation(makeTimelineWorkload(), config);

    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    auto timeline = readJson(filepath);
    EXPECT_EQ(
        objectKeys(timeline),
        (std::vector<std::string>{
            "common_info", "noc_transfers", "timestep_data"}));
    EXPECT_EQ(
        objectKeys(timeline["common_info"]),
        (std::vector<std::string>{
            "congestion_model_name",
            "cycles_per_timestep",
            "device_name",
            "dram_bw_util",
            "link_demand",
            "link_util",
            "max_link_demand",
            "mcast_write_link_util",
            "num_cols",
            "num_rows"}));
    EXPECT_EQ(timeline["common_info"]["device_name"], "wormhole_b0");
    EXPECT_EQ(timeline["common_info"]["cycles_per_timestep"], 128);
    EXPECT_EQ(timeline["common_info"]["num_rows"], 12);
    EXPECT_EQ(timeline["common_info"]["num_cols"], 10);

    ASSERT_EQ(timeline["noc_transfers"].size(), 2);
    EXPECT_EQ(
        objectKeys(timeline["noc_transfers"][0]),
        (std::vector<std::string>{
            "dst",
            "end_cycle",
            "id",
            "injection_rate",
            "noc_event_type",
            "noc_type",
            "route",
            "src",
            "start_cycle",
            "total_bytes"}));
    EXPECT_EQ(timeline["noc_transfers"][0]["id"], 0);
    EXPECT_EQ(
        timeline["noc_transfers"][0]["src"],
        nlohmann::ordered_json::array({1, 1}));
    EXPECT_EQ(
        timeline["noc_transfers"][0]["dst"],
        nlohmann::ordered_json::array({{1, 5}}));
    EXPECT_EQ(timeline["noc_transfers"][0]["total_bytes"], 2048);
    EXPECT_EQ(timeline["noc_transfers"][0]["noc_type"], "NOC1");
    EXPECT_EQ(timeline["noc_transfers"][0]["noc_event_type"], "READ");

    ASSERT_FALSE(timeline["timestep_data"].empty());
    EXPECT_EQ(
        objectKeys(timeline["timestep_data"][0]),
        (std::vector<std::string>{
            "active_transfers",
            "avg_link_demand",
            "avg_link_util",
            "end_cycle",
            "link_demand",
            "mcast_write_link_util",
            "start_cycle"}));
    EXPECT_EQ(timeline["timestep_data"][0]["start_cycle"], 0);
    EXPECT_EQ(timeline["timestep_data"][0]["end_cycle"], 128);
    EXPECT_EQ(
        timeline["timestep_data"][0]["active_transfers"],
        nlohmann::ordered_json::array({0, 1}));
}

TEST_F(TimelineSerializationTest, WritesVersionedFullAndSplitSchemas) {
    auto filepath = temp_dir / "timeline.npeviz";

    npeConfig config;
    config.emit_timeline_file = true;
    config.timeline_filepath = filepath.string();
    config.timeline_split_threshold_timesteps = 1;
    npeEngine engine("wormhole_b0");
    auto result = engine.runPerfEstimation(makeTimelineWorkload(), config);

    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    auto full_timeline = readJson(filepath);
    expectV1TimelineShape(full_timeline);
    ASSERT_GT(full_timeline["timestep_data"].size(), 1);
    EXPECT_FALSE(full_timeline["common_info"].contains("split_info"));

    auto split_timeline = readJson(temp_dir / "timeline_split_0.npeviz");
    EXPECT_FALSE(split_timeline["timestep_data"].empty());
    auto expected_split_keys = objectKeys(full_timeline["common_info"]);
    expected_split_keys.push_back("split_info");
    EXPECT_EQ(objectKeys(split_timeline["common_info"]), expected_split_keys);
    EXPECT_EQ(split_timeline["common_info"]["split_info"]["split_index"], 0);
    EXPECT_EQ(
        split_timeline["common_info"]["split_info"]["total_splits"],
        full_timeline["timestep_data"].size());
    EXPECT_EQ(
        split_timeline["timestep_data"][0]["active_transfers"],
        nlohmann::ordered_json::array({0}));

    auto reconstructed_timesteps = nlohmann::ordered_json::array();
    for (size_t split_index = 0;
         split_index < full_timeline["timestep_data"].size();
         ++split_index) {
        auto split = readJson(
            temp_dir /
            fmt::format("timeline_split_{}.npeviz", split_index));
        ASSERT_EQ(split["timestep_data"].size(), 1);
        reconstructed_timesteps.push_back(split["timestep_data"][0]);
    }
    EXPECT_EQ(reconstructed_timesteps, full_timeline["timestep_data"]);
}

TEST_F(TimelineSerializationTest, WritesCompressedFullAndSplitTimelines) {
    auto filepath = temp_dir / "timeline.npeviz";

    npeConfig config;
    config.emit_timeline_file = true;
    config.timeline_filepath = filepath.string();
    config.timeline_split_threshold_timesteps = 1;
    config.compress_timeline_output_file = true;
    npeEngine engine("wormhole_b0");
    auto result = engine.runPerfEstimation(makeTimelineWorkload(), config);

    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    EXPECT_FALSE(std::filesystem::exists(filepath));
    auto full_timeline = readCompressedJson(filepath.string() + ".zst");
    expectV1TimelineShape(full_timeline);
    ASSERT_GT(full_timeline["timestep_data"].size(), 1);

    for (size_t split_index = 0;
         split_index < full_timeline["timestep_data"].size();
         ++split_index) {
        auto split_path =
            temp_dir / fmt::format("timeline_split_{}.npeviz.zst", split_index);
        ASSERT_TRUE(std::filesystem::exists(split_path));
        auto split_timeline = readCompressedJson(split_path);
        EXPECT_EQ(
            split_timeline["common_info"]["split_info"]["split_index"],
            split_index);
        EXPECT_EQ(
            split_timeline["common_info"]["split_info"]["total_splits"],
            full_timeline["timestep_data"].size());
        EXPECT_TRUE(split_timeline["timestep_data"].is_array());
    }
}

TEST_F(TimelineSerializationTest, SerializationFailureLeavesNoFinalFile) {
    auto filepath = temp_dir / "timeline.npeviz";

    npeConfig config;
    config.device_name = "N300";
    config.emit_timeline_file = true;
    config.timeline_filepath = filepath.string();
    npeEngine engine("N300");
    auto result =
        engine.runPerfEstimation(makeMultichipTimelineWorkload(), config);

    ASSERT_TRUE(std::holds_alternative<npeStats>(result));
    EXPECT_FALSE(std::filesystem::exists(filepath));
    EXPECT_FALSE(std::filesystem::exists(filepath.string() + ".tmp"));
}

}  // namespace
}  // namespace tt_npe

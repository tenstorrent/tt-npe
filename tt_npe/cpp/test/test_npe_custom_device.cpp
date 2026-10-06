// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "device_models/blackhole.hpp"
#include "device_models/custom.hpp"
#include "device_models/wormhole_b0.hpp"
#include "gtest/gtest.h"
#include "npeDeviceModelFactory.hpp"

namespace tt_npe {
namespace {

std::filesystem::path dataDirectory() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data";
}

std::filesystem::path profilerSocDescriptor() {
    return std::filesystem::path(__FILE__).parent_path() / "data" /
           "profiler-blackhole-soc-descriptor.yaml";
}

CustomDeviceModel makeCustomBlackhole(size_t num_chips = 1) {
    auto resolved = resolveNpeDeviceModelConfig(
        dataDirectory() / "device/layout/arch-blackhole.yaml",
        dataDirectory() / "device/models");
    return CustomDeviceModel(std::move(resolved), num_chips);
}

CustomDeviceModel makeCustomWormhole(size_t num_chips = 1) {
    auto resolved = resolveNpeDeviceModelConfig(
        dataDirectory() / "device/layout/arch-wormhole.yaml",
        dataDirectory() / "device/models");
    return CustomDeviceModel(std::move(resolved), num_chips);
}

CustomDeviceModel makeCustomQuasarMesh() {
    auto resolved = resolveNpeDeviceModelConfig(
        std::filesystem::path(__FILE__).parent_path() / "data" /
            "quasar-mesh-4x3-soc-descriptor.yaml",
        dataDirectory() / "device/models");
    return CustomDeviceModel(std::move(resolved));
}

class TemporaryTopology {
   public:
    explicit TemporaryTopology(std::string_view contents) {
        path_ = std::filesystem::temp_directory_path() /
                fmt::format(
                    "tt_npe_topology_{}.json",
                    std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream output(path_);
        output << contents;
    }

    ~TemporaryTopology() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

   private:
    std::filesystem::path path_;
};

TEST(npeCustomDeviceTest, BuildsCoreAndDramLookupsFromSocDescriptor) {
    const auto model = makeCustomBlackhole();

    EXPECT_EQ(model.getArch(), DeviceArch::Blackhole);
    EXPECT_EQ(model.getRows(), 12);
    EXPECT_EQ(model.getCols(), 17);
    EXPECT_EQ(model.getNumChips(), 1);
    EXPECT_EQ(model.getCoreType({0, 0, 0}), CoreType::DRAM);
    EXPECT_EQ(model.getCoreType({0, 1, 1}), CoreType::ETH);
    EXPECT_EQ(model.getCoreType({0, 2, 1}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType({0, 2, 8}), CoreType::UNDEF);
    EXPECT_EQ(model.getDramControllerIDForCore({0, 2, 0}), 1);
}

TEST(npeCustomDeviceTest, LoadsProfilerGeneratedSocDescriptorFormat) {
    auto resolved = resolveNpeDeviceModelConfig(profilerSocDescriptor());
    EXPECT_EQ(resolved.soc_descriptor.arch_name, "BLACKHOLE");
    EXPECT_EQ(resolved.soc_descriptor.worker_l1_size, 1572864);
    EXPECT_EQ(resolved.soc_descriptor.dram_bank_size, 4278190080);
    EXPECT_EQ(resolved.soc_descriptor.eth_l1_size, 524288);

    const CustomDeviceModel model(std::move(resolved));

    EXPECT_EQ(model.getCoreType({0, 2, 1}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType({0, 1, 1}), CoreType::ETH);
    EXPECT_EQ(model.getDramControllerIDForCore({0, 5, 9}), 7);
}

TEST(npeCustomDeviceTest, BuildsQuasarFromProfilerSocDescriptor) {
    auto resolved = resolveNpeDeviceModelConfig(
        std::filesystem::path(__FILE__).parent_path() / "data" /
            "profiler-quasar-soc-descriptor.yaml",
        dataDirectory() / "device/models");
    const CustomDeviceModel model(std::move(resolved));

    EXPECT_EQ(model.getArch(), DeviceArch::Quasar);
    EXPECT_EQ(model.getRows(), 3);
    EXPECT_EQ(model.getCols(), 2);
    EXPECT_EQ(model.getCoreType({0, 0, 0}), CoreType::DRAM);
    EXPECT_EQ(model.getCoreType({0, 0, 1}), CoreType::DRAM);
    EXPECT_EQ(model.getCoreType({0, 1, 0}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType({0, 1, 1}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType({0, 2, 0}), CoreType::UNDEF);
    EXPECT_EQ(model.getDramControllerIDForCore({0, 0, 1}), 1);
    EXPECT_FLOAT_EQ(model.getLinkBandwidth(0), 243.6f);
    EXPECT_EQ(model.getNumNocs(), 1);
}

TEST(npeCustomDeviceTest, TorusModelsKeepTwoNocsWithHalfTheLinksEach) {
    const auto model = makeCustomBlackhole(2);

    EXPECT_EQ(model.getNumNocs(), 2);
    EXPECT_EQ(model.getNumLinksPerChip(nocType::NOC0), 12 * 17 * 2);
    EXPECT_EQ(model.getNumLinksPerChip(nocType::NOC1), 12 * 17 * 2);
    EXPECT_EQ(model.initDeviceState()->getLinkDemandGrid().size(), 2 * 12 * 17 * 4);
}

TEST(npeCustomDeviceTest, MeshHasOnlyInGridNoc0Links) {
    const auto model = makeCustomQuasarMesh();
    const size_t rows = model.getRows();
    const size_t cols = model.getCols();
    ASSERT_EQ(rows, 3);
    ASSERT_EQ(cols, 4);

    // east+west links per row, plus north+south links per column
    const size_t expected_links = 2 * rows * (cols - 1) + 2 * cols * (rows - 1);
    EXPECT_EQ(model.getNumNocs(), 1);
    EXPECT_EQ(model.getNumLinksPerChip(nocType::NOC0), expected_links);
    EXPECT_EQ(model.getNumLinksPerChip(nocType::NOC1), 0);

    const auto state = model.initDeviceState();
    ASSERT_EQ(state->getLinkDemandGrid().size(), expected_links);
    EXPECT_EQ(state->getNIUDemandGrid().size(), rows * cols * 2);

    for (nocLinkID id = 0; id < static_cast<nocLinkID>(expected_links); ++id) {
        const auto& attr = model.getLinkAttributes(id);
        EXPECT_EQ(getNocTypeOfLink(attr.type), nocType::NOC0);
        switch (attr.type) {
            case nocLinkType::NOC0_EAST: EXPECT_LT(attr.coord.col + 1, cols); break;
            case nocLinkType::NOC0_WEST: EXPECT_GT(attr.coord.col, 0); break;
            case nocLinkType::NOC0_SOUTH: EXPECT_LT(attr.coord.row + 1, rows); break;
            case nocLinkType::NOC0_NORTH: EXPECT_GT(attr.coord.row, 0); break;
            default: ADD_FAILURE() << "unexpected link type on mesh"; break;
        }
    }
}

TEST(npeCustomDeviceTest, MeshRoutesXThenYWithoutWraparound) {
    const auto model = makeCustomQuasarMesh();
    auto link = [&model](int row, int col, nocLinkType type) {
        return model.getLinkID({{0, row, col}, type});
    };

    // east, then south
    EXPECT_EQ(
        model.route(nocType::NOC0, {0, 0, 0}, Coord{0, 2, 2}),
        (nocRoute{
            link(0, 0, nocLinkType::NOC0_EAST),
            link(0, 1, nocLinkType::NOC0_EAST),
            link(0, 2, nocLinkType::NOC0_SOUTH),
            link(1, 2, nocLinkType::NOC0_SOUTH)}));

    // west, then north; a torus would wrap east past the last column instead
    EXPECT_EQ(
        model.route(nocType::NOC0, {0, 2, 3}, Coord{0, 0, 1}),
        (nocRoute{
            link(2, 3, nocLinkType::NOC0_WEST),
            link(2, 2, nocLinkType::NOC0_WEST),
            link(2, 1, nocLinkType::NOC0_NORTH),
            link(1, 1, nocLinkType::NOC0_NORTH)}));

    EXPECT_TRUE(model.route(nocType::NOC0, {0, 1, 1}, Coord{0, 1, 1}).empty());
}

TEST(npeCustomDeviceTest, MeshMulticastCoversRectangleAroundSource) {
    const auto model = makeCustomQuasarMesh();
    auto link = [&model](int row, int col, nocLinkType type) {
        return model.getLinkID({{0, row, col}, type});
    };

    auto route = model.route(
        nocType::NOC0, {0, 1, 1}, MulticastCoordSet({0, 0, 1}, {0, 2, 2}));
    std::sort(route.begin(), route.end());

    nocRoute expected = {
        link(1, 1, nocLinkType::NOC0_NORTH),
        link(1, 1, nocLinkType::NOC0_SOUTH),
        link(1, 1, nocLinkType::NOC0_EAST),
        link(1, 2, nocLinkType::NOC0_NORTH),
        link(1, 2, nocLinkType::NOC0_SOUTH)};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(route, expected);
}

TEST(npeCustomDeviceTest, MeshWriteLatencyUsesManhattanHops) {
    const auto model = makeCustomQuasarMesh();
    const auto config = parseNpeDeviceModelConfig(dataDirectory() / "device/models/quasar.yaml");
    const auto& write = config.write_latencies;

    EXPECT_EQ(model.getWriteLatency({0, 1, 1}, {0, 1, 1}, nocType::NOC0), write.startup);
    EXPECT_EQ(
        model.getWriteLatency({0, 2, 3}, {0, 0, 1}, nocType::NOC0),
        write.startup + 4 * write.cycles_per_hop);
    EXPECT_EQ(
        model.getWriteLatency({0, 0, 1}, {0, 2, 3}, nocType::NOC0),
        write.startup + 4 * write.cycles_per_hop);
}

TEST(npeCustomDeviceTest, UsesBlackholeModelConfigValues) {
    const auto model = makeCustomBlackhole();

    EXPECT_FLOAT_EQ(model.getSrcInjectionRate({0, 2, 1}), 60.9f);
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate({0, 0, 0}), 40.0f);
    EXPECT_FLOAT_EQ(model.getLinkBandwidth(0), 60.9f);
    EXPECT_FLOAT_EQ(model.getEthBandwidthPerLink(), 50.0f);
    EXPECT_FLOAT_EQ(model.getDRAMBandwidthPerController(), 40.0f);
    EXPECT_FLOAT_EQ(model.getDRAMBandwidthPerChip(), 320.0f);
}

TEST(npeCustomDeviceTest, BuildsWormholeFromSocDescriptor) {
    const auto model = makeCustomWormhole();

    EXPECT_EQ(model.getArch(), DeviceArch::WormholeB0);
    EXPECT_EQ(model.getRows(), 12);
    EXPECT_EQ(model.getCols(), 10);
    EXPECT_EQ(model.getNumChips(), 1);
    EXPECT_EQ(model.getCoreType({0, 0, 0}), CoreType::DRAM);
    EXPECT_EQ(model.getCoreType({0, 0, 1}), CoreType::ETH);
    EXPECT_EQ(model.getCoreType({0, 1, 1}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType({0, 2, 0}), CoreType::UNDEF);
    EXPECT_EQ(model.getDramControllerIDForCore({0, 1, 0}), 0);
}

TEST(npeCustomDeviceTest, UsesWormholeModelConfigValues) {
    const auto model = makeCustomWormhole();

    EXPECT_FLOAT_EQ(model.getSrcInjectionRate({0, 1, 1}), 28.1f);
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate({0, 0, 0}), 23.2f);
    EXPECT_FLOAT_EQ(model.getSinkAbsorptionRate({0, 0, 0}), 24.0f);
    EXPECT_FLOAT_EQ(model.getLinkBandwidth(0), 30.0f);
    EXPECT_FLOAT_EQ(model.getEthBandwidthPerLink(), 12.5f);
    EXPECT_FLOAT_EQ(model.getDRAMBandwidthPerController(), 47.2f);
    EXPECT_FLOAT_EQ(model.getDRAMBandwidthPerChip(), 283.2f);
}

TEST(npeCustomDeviceTest, RoutesLikeExistingBlackholeModel) {
    const auto custom_model = makeCustomBlackhole();
    const BlackholeDeviceModel existing_model(
        BlackholeDeviceModel::DRAMHarvestingConfig::NO_HARVESTING);
    const Coord start{0, 1, 1};
    const Coord destination{0, 3, 4};

    EXPECT_EQ(
        custom_model.route(nocType::NOC0, start, destination),
        existing_model.route(nocType::NOC0, start, destination));
    EXPECT_EQ(
        custom_model.route(nocType::NOC1, start, destination),
        existing_model.route(nocType::NOC1, start, destination));
}

TEST(npeCustomDeviceTest, RoutesLikeExistingWormholeModel) {
    const auto custom_model = makeCustomWormhole();
    const WormholeB0DeviceModel existing_model;
    const Coord start{0, 1, 1};
    const Coord destination{0, 3, 4};

    EXPECT_EQ(
        custom_model.route(nocType::NOC0, start, destination),
        existing_model.route(nocType::NOC0, start, destination));
    EXPECT_EQ(
        custom_model.route(nocType::NOC1, start, destination),
        existing_model.route(nocType::NOC1, start, destination));
}

TEST(npeCustomDeviceTest, LatenciesMatchExistingBlackholeModel) {
    const auto custom_model = makeCustomBlackhole();
    const BlackholeDeviceModel existing_model(
        BlackholeDeviceModel::DRAMHarvestingConfig::NO_HARVESTING);
    const Coord source{0, 2, 1};
    const std::vector<Coord> destinations = {
        {0, 2, 1}, {0, 4, 1}, {0, 2, 4}, {0, 4, 4}};

    for (const auto& destination : destinations) {
        EXPECT_EQ(
            custom_model.getReadLatency(source, destination),
            existing_model.getReadLatency(source, destination));
        EXPECT_EQ(
            custom_model.getWriteLatency(source, destination, nocType::NOC0),
            existing_model.getWriteLatency(source, destination, nocType::NOC0));
        EXPECT_EQ(
            custom_model.getWriteLatency(source, destination, nocType::NOC1),
            existing_model.getWriteLatency(source, destination, nocType::NOC1));
    }
}

TEST(npeCustomDeviceTest, LatenciesMatchExistingWormholeModel) {
    const auto custom_model = makeCustomWormhole();
    const WormholeB0DeviceModel existing_model;
    const Coord source{0, 1, 1};
    const std::vector<Coord> destinations = {
        {0, 1, 1}, {0, 3, 1}, {0, 1, 4}, {0, 3, 4}};

    for (const auto& destination : destinations) {
        EXPECT_EQ(
            custom_model.getReadLatency(source, destination),
            existing_model.getReadLatency(source, destination));
        EXPECT_EQ(
            custom_model.getWriteLatency(source, destination, nocType::NOC0),
            existing_model.getWriteLatency(source, destination, nocType::NOC0));
        EXPECT_EQ(
            custom_model.getWriteLatency(source, destination, nocType::NOC1),
            existing_model.getWriteLatency(source, destination, nocType::NOC1));
    }
}

TEST(npeCustomDeviceTest, CreatesDistinctLookupIdsForEachChip) {
    const auto model = makeCustomBlackhole(2);

    EXPECT_EQ(model.getDeviceIDs().size(), 2);
    const nocLinkAttr chip_zero_link{{0, 0, 0}, nocLinkType::NOC0_EAST};
    const nocLinkAttr chip_one_link{{1, 0, 0}, nocLinkType::NOC0_EAST};
    const auto chip_zero_id = model.getLinkID(chip_zero_link);
    const auto chip_one_id = model.getLinkID(chip_one_link);
    EXPECT_NE(chip_zero_id, chip_one_id);
    EXPECT_EQ(model.getLinkAttributes(chip_zero_id), chip_zero_link);
    EXPECT_EQ(model.getLinkAttributes(chip_one_id), chip_one_link);
}

TEST(npeCustomDeviceTest, FactoryUsesSocDescriptorForCustomModel) {
    npeConfig cfg;
    cfg.device_name = "P300";
    cfg.soc_descriptor_file =
        (dataDirectory() / "device/layout/arch-blackhole.yaml").string();
    auto model = npeDeviceModelFactory::createDeviceModel(cfg);

    EXPECT_NE(dynamic_cast<CustomDeviceModel*>(model.get()), nullptr);
    EXPECT_EQ(model->getNumChips(), 2);
}

TEST(npeCustomDeviceTest, FactoryKeepsExistingModelWithoutSocDescriptor) {
    auto model = npeDeviceModelFactory::createDeviceModel("P150");

    EXPECT_NE(dynamic_cast<BlackholeDeviceModel*>(model.get()), nullptr);
    EXPECT_EQ(dynamic_cast<CustomDeviceModel*>(model.get()), nullptr);
}

TEST(npeCustomDeviceTest, FactoryUsesDeviceIDsFromTopology) {
    const TemporaryTopology topology(R"({
        "device_id_to_fabric_node_id": {
            "2": [0, 0],
            "7": [0, 1]
        }
    })");
    npeConfig cfg;
    cfg.device_name = "P150";
    cfg.soc_descriptor_file =
        (dataDirectory() / "device/layout/arch-blackhole.yaml").string();
    cfg.topology_json = topology.path().string();

    const auto model = npeDeviceModelFactory::createDeviceModel(cfg);

    EXPECT_EQ(model->getNumChips(), 2);
    EXPECT_TRUE(model->isValidDeviceID(2));
    EXPECT_TRUE(model->isValidDeviceID(7));
    EXPECT_FALSE(model->isValidDeviceID(0));
}

TEST(npeCustomDeviceTest, CongestionMatchesExistingBlackholeModel) {
    const auto custom_model = makeCustomBlackhole();
    const BlackholeDeviceModel existing_model(
        BlackholeDeviceModel::DRAMHarvestingConfig::NO_HARVESTING);
    const Coord source{0, 2, 1};
    const Coord destination{0, 2, 4};
    const npeWorkloadTransfer transfer(
        16384, 1, source, destination, 60.9f, 0, nocType::NOC0);

    std::vector<PETransferState> custom_transfers;
    std::vector<PETransferState> existing_transfers;
    for (int transfer_index = 0; transfer_index < 2; ++transfer_index) {
        custom_transfers.emplace_back(
            transfer,
            0,
            custom_model.route(nocType::NOC0, source, destination));
        existing_transfers.emplace_back(
            transfer,
            0,
            existing_model.route(nocType::NOC0, source, destination));
    }

    auto custom_state = custom_model.initDeviceState();
    auto existing_state = existing_model.initDeviceState();
    const std::vector<PETransferID> live_transfer_ids = {0, 1};
    custom_model.computeCurrentTransferRate(
        0,
        128,
        custom_transfers,
        live_transfer_ids,
        *custom_state,
        true);
    existing_model.computeCurrentTransferRate(
        0,
        128,
        existing_transfers,
        live_transfer_ids,
        *existing_state,
        true);

    ASSERT_EQ(custom_transfers.size(), existing_transfers.size());
    for (size_t index = 0; index < custom_transfers.size(); ++index) {
        EXPECT_FLOAT_EQ(
            custom_transfers[index].curr_bandwidth,
            existing_transfers[index].curr_bandwidth);
    }
    EXPECT_EQ(
        custom_state->getNIUDemandGrid(),
        existing_state->getNIUDemandGrid());
    EXPECT_EQ(
        custom_state->getLinkDemandGrid(),
        existing_state->getLinkDemandGrid());
}

TEST(npeCustomDeviceTest, CongestionMatchesExistingWormholeModel) {
    const auto custom_model = makeCustomWormhole();
    const WormholeB0DeviceModel existing_model;
    const Coord source{0, 1, 1};
    const Coord destination{0, 1, 4};
    const npeWorkloadTransfer transfer(
        8192, 1, source, destination, 28.1f, 0, nocType::NOC0);

    std::vector<PETransferState> custom_transfers;
    std::vector<PETransferState> existing_transfers;
    for (int transfer_index = 0; transfer_index < 2; ++transfer_index) {
        custom_transfers.emplace_back(
            transfer,
            0,
            custom_model.route(nocType::NOC0, source, destination));
        existing_transfers.emplace_back(
            transfer,
            0,
            existing_model.route(nocType::NOC0, source, destination));
    }

    auto custom_state = custom_model.initDeviceState();
    auto existing_state = existing_model.initDeviceState();
    const std::vector<PETransferID> live_transfer_ids = {0, 1};
    custom_model.computeCurrentTransferRate(
        0,
        128,
        custom_transfers,
        live_transfer_ids,
        *custom_state,
        true);
    existing_model.computeCurrentTransferRate(
        0,
        128,
        existing_transfers,
        live_transfer_ids,
        *existing_state,
        true);

    ASSERT_EQ(custom_transfers.size(), existing_transfers.size());
    for (size_t index = 0; index < custom_transfers.size(); ++index) {
        EXPECT_FLOAT_EQ(
            custom_transfers[index].curr_bandwidth,
            existing_transfers[index].curr_bandwidth);
    }
    EXPECT_EQ(
        custom_state->getNIUDemandGrid(),
        existing_state->getNIUDemandGrid());
    EXPECT_EQ(
        custom_state->getLinkDemandGrid(),
        existing_state->getLinkDemandGrid());
}

}  // namespace
}  // namespace tt_npe

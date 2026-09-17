// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "device_models/blackhole.hpp"
#include "device_models/custom.hpp"
#include "gtest/gtest.h"
#include "npeDeviceModelFactory.hpp"

namespace tt_npe {
namespace {

std::filesystem::path dataDirectory() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data";
}

CustomDeviceModel makeCustomBlackhole(size_t num_chips = 1) {
    auto resolved = resolveNpeDeviceModelConfig(
        dataDirectory() / "device/layout/arch-blackhole.yaml",
        dataDirectory() / "device/models");
    return CustomDeviceModel(std::move(resolved), num_chips);
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

TEST(npeCustomDeviceTest, UsesBlackholeModelConfigValues) {
    const auto model = makeCustomBlackhole();

    EXPECT_FLOAT_EQ(model.getSrcInjectionRate({0, 2, 1}), 60.9f);
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate({0, 0, 0}), 40.0f);
    EXPECT_FLOAT_EQ(model.getLinkBandwidth(0), 60.9f);
    EXPECT_FLOAT_EQ(model.getEthBandwidthPerLink(), 50.0f);
    EXPECT_FLOAT_EQ(model.getDRAMBandwidthPerController(), 40.0f);
    EXPECT_FLOAT_EQ(model.getDRAMBandwidthPerChip(), 320.0f);
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
    auto model = npeDeviceModelFactory::createDeviceModel(
        "P300", dataDirectory() / "device/layout/arch-blackhole.yaml");

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

}  // namespace
}  // namespace tt_npe

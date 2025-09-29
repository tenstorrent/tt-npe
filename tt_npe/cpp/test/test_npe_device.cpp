// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include <boost/unordered/unordered_flat_set.hpp>

#include "gtest/gtest.h"
#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "device_models/blackhole.hpp"
#include "npeEngine.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

TEST(npeDeviceTest, CanConstructWormholeB0Device) { 
    WormholeB0DeviceModel(); 
}
TEST(npeDeviceTest, CanErrOutOnUndefinedDevice) {
    npeConfig cfg;
    cfg.device_name = "undef";
    EXPECT_THROW((npeEngine(cfg)), npeException);
}
TEST(npeDeviceTest, CanRouteWormholeB0Noc) {
    WormholeB0DeviceModel model;

    for (int i = 0; i < 100; i++) {
        Coord start = {model.getDeviceID(), wrapToRange(rand(), model.getRows()), wrapToRange(rand(), model.getCols())};
        Coord end = {model.getDeviceID(), wrapToRange(rand(), model.getRows()), wrapToRange(rand(), model.getCols())};
        model.route(nocType::NOC0, start, end);
    }
}
TEST(npeDeviceTest, CanGetCoreTypeWormholeB0) {
    WormholeB0DeviceModel model;

    // check that the entire grid can be queried successfully
    for (int r = 0; r < model.getRows(); r++) {
        for (int c = 0; c < model.getCols(); c++) {
            model.getCoreType({model.getDeviceID(), r, c});
        }
    }

    // test a few known locations
    EXPECT_EQ(model.getCoreType(Coord{model.getDeviceID(), 0, 1}), CoreType::ETH);
    EXPECT_EQ(model.getCoreType(Coord{model.getDeviceID(), 1, 0}), CoreType::DRAM);
    EXPECT_EQ(model.getCoreType(Coord{model.getDeviceID(), 1, 1}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType(Coord{model.getDeviceID(), 10, 0}), CoreType::UNDEF);
}
TEST(npeDeviceTest, CanGetSrcInjectionRateWormholeB0) {
    WormholeB0DeviceModel model;

    // check that the entire grid can be queried successfully
    for (int r = 0; r < model.getRows(); r++) {
        for (int c = 0; c < model.getCols(); c++) {
            model.getSrcInjectionRate({model.getDeviceID(), r, c});
        }
    }

    // test a few known locations
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{model.getDeviceID(), 1, 0}), 23.2);
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{model.getDeviceID(), 1, 1}), 28.1);
}
TEST(npeDeviceTest, TestLinkIDLookups) {
    WormholeB0DeviceModel model;

    // check that the entire grid can be queried successfully
    boost::unordered_flat_set<nocLinkID> links_seen;
    for (int r = 0; r < model.getRows(); r++) {
        for (int c = 0; c < model.getCols(); c++) {
            for (const auto& link_type : model.getLinkTypes()) {
                auto id = model.getLinkID({{model.getDeviceID(), r, c}, link_type});
                GTEST_ASSERT_TRUE(not links_seen.contains(id));
                links_seen.insert(id);
            }
        }
    }
    boost::unordered_flat_set<nocLinkAttr> attrs_seen;
    for (size_t id=0; id < links_seen.size(); id++) {
        auto attr = model.getLinkAttributes(nocLinkID(id));
        GTEST_ASSERT_TRUE(not attrs_seen.contains(attr));
        attrs_seen.insert(attr);
    }
}

// Tests for WormholeMultichipDeviceModel
TEST(npeDeviceTest, CanConstructWormholeMultichipDevice) { 
    WormholeMultichipDeviceModel model_2chip(2);
    WormholeMultichipDeviceModel model_8chip(8);
}

TEST(npeDeviceTest, CanGetCoreTypeWormholeMultichip) {
    constexpr size_t num_chips_to_test = 2;
    WormholeMultichipDeviceModel model(num_chips_to_test);

    // check that the entire grid for all chips can be queried successfully
    for (size_t dev_id = 0; dev_id < model.getNumChips(); ++dev_id) {
        for (int r = 0; r < model.getRows(); r++) {
            for (int c = 0; c < model.getCols(); c++) {
                model.getCoreType({static_cast<DeviceID>(dev_id), r, c});
            }
        }
    }

    // test a few known locations on different devices
    EXPECT_EQ(model.getCoreType(Coord{0, 0, 1}), CoreType::ETH);
    EXPECT_EQ(model.getCoreType(Coord{0, 1, 0}), CoreType::DRAM);
    EXPECT_EQ(model.getCoreType(Coord{0, 1, 1}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType(Coord{0, 10, 0}), CoreType::UNDEF); // Assuming 10 is out of bounds for rows

    if (num_chips_to_test > 1) {
        EXPECT_EQ(model.getCoreType(Coord{1, 0, 1}), CoreType::ETH);
        EXPECT_EQ(model.getCoreType(Coord{1, 1, 0}), CoreType::DRAM);
        EXPECT_EQ(model.getCoreType(Coord{1, 1, 1}), CoreType::WORKER);
    }
}

TEST(npeDeviceTest, CanGetSrcInjectionRateWormholeMultichip) {
    constexpr size_t num_chips_to_test = 2;
    WormholeMultichipDeviceModel model(num_chips_to_test);

    // check that the entire grid for all chips can be queried successfully
    for (size_t dev_id = 0; dev_id < model.getNumChips(); ++dev_id) {
        for (int r = 0; r < model.getRows(); r++) {
            for (int c = 0; c < model.getCols(); c++) {
                model.getSrcInjectionRate({static_cast<DeviceID>(dev_id), r, c});
            }
        }
    }

    // test a few known locations on different devices
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{0, 1, 0}), 23.2);
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{0, 1, 1}), 28.1);

    if (num_chips_to_test > 1) {
        EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{1, 1, 0}), 23.2);
        EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{1, 1, 1}), 28.1);
    }
}

TEST(npeDeviceTest, TestLinkIDLookupsWormholeMultichip) {
    constexpr size_t num_chips_to_test = 2;
    WormholeMultichipDeviceModel model(num_chips_to_test);

    boost::unordered_flat_set<nocLinkID> links_seen;
    for (size_t dev_id = 0; dev_id < model.getNumChips(); ++dev_id) {
        for (int r = 0; r < model.getRows(); r++) {
            for (int c = 0; c < model.getCols(); c++) {
                for (const auto& link_type : model.getLinkTypes()) {
                    auto id = model.getLinkID({{static_cast<DeviceID>(dev_id), r, c}, link_type});
                    // Link IDs should be unique across chips in the multichip model's global lookup
                    GTEST_ASSERT_TRUE(not links_seen.contains(id)); 
                    links_seen.insert(id);
                }
            }
        }
    }

    boost::unordered_flat_set<nocLinkAttr> attrs_seen;
    // The link_id_to_attr_lookup in WormholeMultichipDeviceModel is global for all chips.
    // So, we iterate up to the total number of links across all chips.
    // Note: This assumes link IDs are dense from 0 to N-1 where N is total links.
    // WormholeMultichipDeviceModel::populateNoCLinkLookups shows link_id_to_attr_lookup.push_back(),
    // and link_attr_to_id_lookup[attr] = link_id_to_attr_lookup.size() - 1; suggesting dense IDs.
    size_t total_links = model.getNumChips() * model.getRows() * model.getCols() * model.getLinkTypes().size();
    
    // We need to access the internal _wormhole_b0_model to know the number of links per chip
    // or calculate it based on getRows, getCols, getLinkTypes from the multichip model itself.
    // The current getLinkAttributes and getLinkID in WormholeMultichipDeviceModel might be tricky for this test part.
    // The test should verify that for each valid ID, a unique attribute is returned, and vice-versa.
    // The WormholeMultichipDeviceModel uses a single vector `link_id_to_attr_lookup` for all chips.

    for (size_t id = 0; id < links_seen.size(); ++id) { // Iterate over collected unique link IDs
        auto attr = model.getLinkAttributes(nocLinkID(id));
        GTEST_ASSERT_TRUE(not attrs_seen.contains(attr));
        attrs_seen.insert(attr);
        // Also verify that getting ID from attr gives back the same ID
        EXPECT_EQ(model.getLinkID(attr), nocLinkID(id));
    }
}
TEST(npeDeviceTest, CanQueryArchUsingDeviceModel) {
    WormholeB0DeviceModel wh_model;
    EXPECT_EQ(wh_model.getArch(), DeviceArch::WormholeB0);
    WormholeMultichipDeviceModel wh_multichip_model(2);
    EXPECT_EQ(wh_multichip_model.getArch(), DeviceArch::WormholeB0);
    BlackholeDeviceModel blackhole_model_p150(BlackholeDeviceModel::Model::p150);
    EXPECT_EQ(blackhole_model_p150.getArch(), DeviceArch::Blackhole);
}
}  // namespace tt_npe

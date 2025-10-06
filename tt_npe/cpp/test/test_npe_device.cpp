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

TEST(npeDeviceTest, ExhaustivePTPRoutesBidirMesh) {
    WormholeB0DeviceConfig cfg;
    cfg.exp_wh_noc_topo = NocTopology::BIDIR_MESH;
    WormholeB0DeviceModel model(cfg);

    auto verify_all_pairs_for_noc = [&](nocType noc) {
        for (int sr = 0; sr < model.getRows(); ++sr) {
            for (int sc = 0; sc < model.getCols(); ++sc) {
                for (int er = 0; er < model.getRows(); ++er) {
                    for (int ec = 0; ec < model.getCols(); ++ec) {
                        Coord start{model.getDeviceID(), sr, sc};
                        Coord end{model.getDeviceID(), er, ec};

                        auto route = model.route(noc, start, end);

                        int row_delta = (er >= sr) ? (er - sr) : (sr - er);
                        int col_delta = (ec >= sc) ? (ec - sc) : (sc - ec);
                        int expected_hops = row_delta + col_delta;
                        ASSERT_EQ(static_cast<int>(route.size()), expected_hops);

                        int cur_r = sr;
                        int cur_c = sc;
                        for (int i = 0; i < expected_hops; ++i) {
                            auto link_id = route[static_cast<size_t>(i)];
                            auto attr = model.getLinkAttributes(link_id);

                            // Expect row moves first, then column moves, per unicastRouteBidirMesh
                            bool expect_row_move = (i < row_delta);
                            nocLinkType expected_type = expect_row_move ? nocLinkType::NOC1_NORTH
                                                                        : nocLinkType::NOC1_WEST;

                            EXPECT_EQ(attr.coord.device_id, model.getDeviceID());
                            EXPECT_EQ(static_cast<int>(attr.coord.row), cur_r);
                            EXPECT_EQ(static_cast<int>(attr.coord.col), cur_c);
                            EXPECT_EQ(attr.type, expected_type);

                            // Check adjacency (no wrap): next coord differs by exactly 1 in one axis
                            int next_r = cur_r + (expect_row_move ? ((er > sr) ? 1 : -1) : 0);
                            int next_c = cur_c + (expect_row_move ? 0 : ((ec > sc) ? 1 : -1));
                            int dr = next_r - cur_r;
                            int dc = next_c - cur_c;
                            EXPECT_TRUE((dr == 0 && (dc == 1 || dc == -1)) ||
                                        (dc == 0 && (dr == 1 || dr == -1)));
                            EXPECT_GE(next_r, 0);
                            EXPECT_LT(next_r, model.getRows());
                            EXPECT_GE(next_c, 0);
                            EXPECT_LT(next_c, model.getCols());

                            // Update current position to reflect the movement
                            if (expect_row_move) {
                                cur_r += (er > sr) ? 1 : -1;
                            } else {
                                cur_c += (ec > sc) ? 1 : -1;
                            }
                        }

                        EXPECT_EQ(cur_r, er);
                        EXPECT_EQ(cur_c, ec);
                    }
                }
            }
        }
    };

    // Verify for both NoCs to ensure consistency in BIDIR_MESH mode
    verify_all_pairs_for_noc(nocType::NOC0);
    verify_all_pairs_for_noc(nocType::NOC1);
}
}  // namespace tt_npe

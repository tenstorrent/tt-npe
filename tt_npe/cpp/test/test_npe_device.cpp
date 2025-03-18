// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "npeCommon.hpp"
#include "device_models/wormhole_b0.hpp"
#include "npeEngine.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

TEST(npeDeviceTest, CanConstructWormholeB0Device) { 
    WormholeB0DeviceModel(); 
}
TEST(npeDeviceTest, CanErrOutOnUndefinedDevice) {
    EXPECT_THROW(npeEngine("undef"), npeException);
}
TEST(npeDeviceTest, CanRouteWormholeB0Noc) {
    WormholeB0DeviceModel model;

    for (int i = 0; i < 100; i++) {
        Coord start = {wrapToRange(rand(), model.getRows()), wrapToRange(rand(), model.getCols())};
        Coord end = {wrapToRange(rand(), model.getRows()), wrapToRange(rand(), model.getCols())};
        model.route(nocType::NOC0, start, end);
    }
}
TEST(npeDeviceTest, CanGetCoreTypeWormholeB0) {
    WormholeB0DeviceModel model;

    // check that the entire grid can be queried successfully
    for (int r = 0; r < model.getRows(); r++) {
        for (int c = 0; c < model.getCols(); c++) {
            model.getCoreType({r, c});
        }
    }

    // test a few known locations
    EXPECT_EQ(model.getCoreType(Coord{0, 1}), CoreType::ETH);
    EXPECT_EQ(model.getCoreType(Coord{1, 0}), CoreType::DRAM);
    EXPECT_EQ(model.getCoreType(Coord{1, 1}), CoreType::WORKER);
    EXPECT_EQ(model.getCoreType(Coord{10, 0}), CoreType::UNDEF);
}
TEST(npeDeviceTest, CanGetSrcInjectionRateWormholeB0) {
    WormholeB0DeviceModel model;

    // check that the entire grid can be queried successfully
    for (int r = 0; r < model.getRows(); r++) {
        for (int c = 0; c < model.getCols(); c++) {
            model.getSrcInjectionRate({r, c});
        }
    }

    // test a few known locations
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{1, 0}), 23.2);
    EXPECT_FLOAT_EQ(model.getSrcInjectionRate(Coord{1, 1}), 28.1);
}

}  // namespace tt_npe
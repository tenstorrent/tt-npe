// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "device_models/wormhole_b0.hpp"
#include "npeWorkload.hpp"

namespace tt_npe {

TEST(npeWorkloadTest, CanConstructWorkload) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    EXPECT_EQ(wl.getPhases().size(), 1);
    EXPECT_EQ(wl.getPhases()[0].transfers.size(), 1);
}
TEST(npeWorkloadTest, CanValidateWorkload) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_TRUE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidTransferSrc) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(
        npeWorkloadTransfer(2048, 1, {DeviceID(), 1, 100}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidTransferDst) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(
        npeWorkloadTransfer(2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 100}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidNumPackets) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(2048, 0, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidPacketSize) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(0, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
}  // namespace tt_npe
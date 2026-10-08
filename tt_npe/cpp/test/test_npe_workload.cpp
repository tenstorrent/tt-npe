// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <filesystem>
#include <fstream>

#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "gtest/gtest.h"
#include "ingestWorkload.hpp"
#include "npeWorkload.hpp"

namespace tt_npe {

// An empty golden map leaves the mesh-wide range at its sentinel initial
// values: golden_start stays at Cycle::max() and golden_end at 0. Cycle is
// uint64_t, so npeStats computes golden_end - golden_start and underflows to
// 1 -- which passes the `golden_cycles > 0` guard and is reported as a real
// measurement.
TEST(npeWorkloadTest, EmptyGoldenResultCyclesDoNotProduceASentinelMeshRange) {
    tt_npe::npeWorkload wl;
    wl.setGoldenResultCycles({});

    auto [golden_start, golden_end] = wl.getGoldenResultCycles(MESH_DEVICE);
    EXPECT_LE(golden_start, golden_end)
        << "mesh golden range is inverted: start=" << golden_start << " end=" << golden_end;
    EXPECT_EQ(golden_end - golden_start, 0u)
        << "mesh golden cycle count underflowed to " << (golden_end - golden_start);
}

TEST(npeWorkloadTest, CanConstructWorkload) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1, "READ", "", -1));
    wl.addPhase(phase);

    EXPECT_EQ(wl.getPhases().size(), 1);
    EXPECT_EQ(wl.getPhases()[0].transfers.size(), 1);
}
TEST(npeWorkloadTest, CanValidateWorkload) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_TRUE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidTransferSrc) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 1, {DeviceID(), 1, 100}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidTransferDst) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 100}, 28.1, 0, nocType::NOC1, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidNumPackets) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 0, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidPacketSize) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        0, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocType::NOC1, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}

TEST(npeWorkloadTest, CanRejectMismatchedDeviceIds) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048,
        1,
        {DeviceID(1), 1, 1},
        Coord{DeviceID(2), 1, 5},
        28.1,
        0,
        nocType::NOC1,
        "READ",
        "",
        -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}

TEST(npeWorkloadTest, CanRejectInvalidSourceDeviceId) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048,
        1,
        {DeviceID(100), 1, 1},
        Coord{DeviceID(), 1, 5},
        28.1,
        0,
        nocType::NOC1,
        "READ",
        "",
        -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}

TEST(npeWorkloadTest, CanRejectInvalidSourceDeviceIdMultiChip) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048,
        1,
        {DeviceID(100), 1, 1},
        Coord{DeviceID(), 1, 5},
        28.1,
        0,
        nocType::NOC1,
        "READ",
        "",
        -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}


TEST(npeWorkloadTest, CanCountRouteHops) {
    // Test NOC_0 routing (clockwise)
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 1, 1, "NOC_0"), 0);   // Same point
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 3, 1, "NOC_0"), 2);   // Horizontal only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 1, 3, "NOC_0"), 2);   // Vertical only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 3, 3, "NOC_0"), 4);   // Diagonal
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(9, 1, 1, 1, "NOC_0"), 2);   // Wrap around
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(5, 1, 4, 1, "NOC_0"), 9);   // Wrap around
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 6, 1, 5, "NOC_0"), 11);  // Vertical wrap

    // Test NOC_1 routing (counter-clockwise)
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 1, 1, "NOC_1"), 0);   // Same point
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(3, 1, 1, 1, "NOC_1"), 2);   // Horizontal only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 3, 1, 1, "NOC_1"), 2);   // Vertical only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(3, 3, 1, 1, "NOC_1"), 4);   // Diagonal
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 9, 11, "NOC_1"), 4);  // Wrap around
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 3, 1, 11, "NOC_1"), 4);  // Vertical wrap
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(3, 1, 9, 1, "NOC_1"), 4);   // Horizontal wrap

    // Test invalid NoC type
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 2, 2, "INVALID"), -1);
}

TEST(npeWorkloadTest, CanIngestAndValidateMultichipTraceFile) {

    // Test ingesting the trace file
    auto workload = createWorkloadFromJSON("cpp/test/data/multichip-trace-example.json", "T3K", true);
    EXPECT_TRUE(workload.has_value());

    // Validate the ingested workload
    auto dm = tt_npe::WormholeMultichipDeviceModel(8);
    EXPECT_TRUE(workload->validate(dm));
}

}  // namespace tt_npe
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

TEST(npeWorkloadTest, CanFilterTraceToCycleWindow) {
    const std::string trace_file = "cpp/test/data/mcast-util-trace-small.json";

    // trace contains 5 noc events at cycles 100,200,300,400,500 within a kernel zone
    // spanning cycles [0,680]
    auto full_workload = createWorkloadFromJSON(trace_file, "wormhole_b0", true);
    ASSERT_TRUE(full_workload.has_value());
    EXPECT_EQ(full_workload->getPhases().at(0).transfers.size(), 5);
    EXPECT_EQ(full_workload->getGoldenResultCycles(MESH_DEVICE), std::make_pair(Cycle(0), Cycle(680)));

    // cycle window bounds are inclusive; expect the events at cycles 200,300,400
    auto windowed_workload =
        createWorkloadFromJSON(trace_file, "wormhole_b0", true, false, CycleWindow{200, 400});
    ASSERT_TRUE(windowed_workload.has_value());
    EXPECT_EQ(windowed_workload->getPhases().at(0).transfers.size(), 3);

    // golden cycles should be clamped to the window so stats cover only the filtered region
    EXPECT_EQ(
        windowed_workload->getGoldenResultCycles(MESH_DEVICE), std::make_pair(Cycle(200), Cycle(400)));

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_TRUE(windowed_workload->validate(dm));
}

TEST(npeWorkloadTest, CanFilterTraceToOpenEndedCycleWindow) {
    const std::string trace_file = "cpp/test/data/mcast-util-trace-small.json";

    // leaving the end of the window unset reads through to the end of the trace
    auto windowed_workload =
        createWorkloadFromJSON(trace_file, "wormhole_b0", true, false, CycleWindow{.start = 300});
    ASSERT_TRUE(windowed_workload.has_value());
    EXPECT_EQ(windowed_workload->getPhases().at(0).transfers.size(), 3);
    EXPECT_EQ(
        windowed_workload->getGoldenResultCycles(MESH_DEVICE), std::make_pair(Cycle(300), Cycle(680)));
}

TEST(npeWorkloadTest, CanRejectCycleWindowContainingNoTransfers) {
    auto windowed_workload = createWorkloadFromJSON(
        "cpp/test/data/mcast-util-trace-small.json", "wormhole_b0", true, false, CycleWindow{5000, 6000});
    EXPECT_FALSE(windowed_workload.has_value());
}

TEST(npeWorkloadTest, GivesDevicesWithNoEventsAnEmptyCycleWindow) {
    // the trace only holds events for device 0, but the T3K model has 8 devices
    auto workload = createWorkloadFromJSON("cpp/test/data/mcast-util-trace-small.json", "T3K", true);
    ASSERT_TRUE(workload.has_value());
    EXPECT_EQ(workload->getGoldenResultCycles(0), std::make_pair(Cycle(0), Cycle(680)));

    for (DeviceID device_id = 1; device_id < 8; device_id++) {
        EXPECT_EQ(workload->getGoldenResultCycles(device_id), std::make_pair(Cycle(0), Cycle(0)))
            << "device " << device_id << " has no events; its cycle window should be empty";
    }

    // devices with no events must not stretch the window covering the whole mesh
    EXPECT_EQ(workload->getGoldenResultCycles(MESH_DEVICE), std::make_pair(Cycle(0), Cycle(680)));
}

TEST(npeWorkloadTest, LeavesEmptyCycleWindowsAloneWhenFiltering) {
    auto workload = createWorkloadFromJSON(
        "cpp/test/data/mcast-util-trace-small.json", "T3K", true, false, CycleWindow{200, 400});
    ASSERT_TRUE(workload.has_value());
    EXPECT_EQ(workload->getGoldenResultCycles(0), std::make_pair(Cycle(200), Cycle(400)));
    EXPECT_EQ(workload->getGoldenResultCycles(1), std::make_pair(Cycle(0), Cycle(0)));
    EXPECT_EQ(workload->getGoldenResultCycles(MESH_DEVICE), std::make_pair(Cycle(200), Cycle(400)));
}

TEST(npeWorkloadTest, CanRejectInvalidCycleWindow) {
    auto workload = createWorkloadFromJSON(
        "cpp/test/data/mcast-util-trace-small.json", "wormhole_b0", true, false, CycleWindow{400, 200});
    EXPECT_FALSE(workload.has_value());
}

}  // namespace tt_npe
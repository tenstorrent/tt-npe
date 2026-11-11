// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

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
        2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocIndex{1}, "READ", "", -1));
    wl.addPhase(phase);

    EXPECT_EQ(wl.getPhases().size(), 1);
    EXPECT_EQ(wl.getPhases()[0].transfers.size(), 1);
}
TEST(npeWorkloadTest, CanValidateWorkload) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocIndex{1}, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_TRUE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidTransferSrc) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 1, {DeviceID(), 1, 100}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocIndex{1}, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidTransferDst) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 100}, 28.1, 0, nocIndex{1}, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidNumPackets) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        2048, 0, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocIndex{1}, "READ", "", -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}
TEST(npeWorkloadTest, CanRejectInvalidPacketSize) {
    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(
        0, 1, {DeviceID(), 1, 1}, Coord{DeviceID(), 1, 5}, 28.1, 0, nocIndex{1}, "READ", "", -1));
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
        nocIndex{1},
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
        nocIndex{1},
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
        nocIndex{1},
        "READ",
        "",
        -1));
    wl.addPhase(phase);

    auto dm = tt_npe::WormholeB0DeviceModel();
    EXPECT_FALSE(wl.validate(dm));
}


TEST(npeWorkloadTest, CanCountRouteHops) {
    // Test NoC 0 routing (clockwise)
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 1, 1, nocIndex{0}), 0);   // Same point
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 3, 1, nocIndex{0}), 2);   // Horizontal only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 1, 3, nocIndex{0}), 2);   // Vertical only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 3, 3, nocIndex{0}), 4);   // Diagonal
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(9, 1, 1, 1, nocIndex{0}), 2);   // Wrap around
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(5, 1, 4, 1, nocIndex{0}), 9);   // Wrap around
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 6, 1, 5, nocIndex{0}), 11);  // Vertical wrap

    // Test NoC 1 routing (counter-clockwise)
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 1, 1, nocIndex{1}), 0);   // Same point
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(3, 1, 1, 1, nocIndex{1}), 2);   // Horizontal only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 3, 1, 1, nocIndex{1}), 2);   // Vertical only
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(3, 3, 1, 1, nocIndex{1}), 4);   // Diagonal
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 9, 11, nocIndex{1}), 4);  // Wrap around
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 3, 1, 11, nocIndex{1}), 4);  // Vertical wrap
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(3, 1, 9, 1, nocIndex{1}), 4);   // Horizontal wrap

    // Test invalid NoC index
    EXPECT_EQ(WormholeB0DeviceModel::route_hops(1, 1, 2, 2, nocIndex{2}), -1);
}

TEST(npeWorkloadTest, CanIngestAndValidateMultichipTraceFile) {

    // Test ingesting the trace file
    auto workload = createWorkloadFromJSON("cpp/test/data/multichip-trace-example.json", "T3K", true);
    EXPECT_TRUE(workload.has_value());

    // Validate the ingested workload
    auto dm = tt_npe::WormholeMultichipDeviceModel(8);
    EXPECT_TRUE(workload->validate(dm));
}

namespace {

// Writes a noc trace to a fresh temporary directory and ingests it
std::optional<npeWorkload> ingestTrace(
    const std::string &test_name, const std::string &trace_json) {
    const auto directory =
        std::filesystem::temp_directory_path() / fmt::format("tt_npe_ingest_{}", test_name);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory);

    const auto trace_path = directory / "noc_trace.json";
    {
        std::ofstream out(trace_path);
        out << trace_json;
    }
    auto workload = createWorkloadFromJSON(trace_path.string(), "wormhole_b0", true);
    std::filesystem::remove_all(directory, error);
    return workload;
}

std::string writeEvent(const std::string &proc, int sx, int sy, int timestamp) {
    return fmt::format(
        R"({{"proc":"{}","sx":{},"sy":{},"dx":1,"dy":5,"noc":"NOC_0",)"
        R"("type":"WRITE_","num_bytes":64,"timestamp":{}}})",
        proc,
        sx,
        sy,
        timestamp);
}

std::string zoneEvent(
    const std::string &proc,
    int sx,
    int sy,
    const std::string &zone,
    const std::string &phase,
    int timestamp) {
    return fmt::format(
        R"({{"proc":"{}","sx":{},"sy":{},"zone":"{}","zone_phase":"{}","timestamp":{}}})",
        proc,
        sx,
        sy,
        zone,
        phase,
        timestamp);
}

std::vector<std::string> zonePaths(const npeWorkload &wl) {
    std::vector<std::string> paths;
    for (const auto &phase : wl.getPhases()) {
        for (const auto &transfer : phase.transfers) {
            paths.push_back(transfer.enclosing_zone_path);
        }
    }
    return paths;
}

}  // namespace

TEST(npeWorkloadTest, CanIngestTraceWithoutZones) {
    auto workload = ingestTrace(
        "no_zones",
        "[" + writeEvent("NCRISC", 1, 1, 100) + "," + writeEvent("BRISC", 2, 1, 120) + "]");
    ASSERT_TRUE(workload.has_value());
    EXPECT_TRUE(workload->getZones().empty());
    EXPECT_EQ(zonePaths(*workload), (std::vector<std::string>{"", ""}));
}

TEST(npeWorkloadTest, CoreWithoutZonesDoesNotInheritZonesOfPreviousCore) {
    // zone_a is never closed (e.g. truncated profiler buffer), so it is still
    // open when the next core is processed
    auto workload = ingestTrace(
        "zone_less_core",
        "[" + zoneEvent("NCRISC", 1, 1, "zone_a", "ZONE_START", 0) + "," +
            writeEvent("NCRISC", 1, 1, 100) + "," +
            writeEvent("NCRISC", 2, 1, 150) + "]");
    ASSERT_TRUE(workload.has_value());
    EXPECT_EQ(zonePaths(*workload), (std::vector<std::string>{"zone_a[0]", ""}));
}

TEST(npeWorkloadTest, MultipleCoresKeepTheirOwnZones) {
    auto workload = ingestTrace(
        "multi_core_zones",
        "[" + zoneEvent("NCRISC", 1, 1, "zone_a", "ZONE_START", 0) + "," +
            writeEvent("NCRISC", 1, 1, 100) + "," +
            zoneEvent("NCRISC", 1, 1, "zone_a", "ZONE_END", 700) + "," +
            zoneEvent("BRISC", 1, 1, "zone_b", "ZONE_START", 10) + "," +
            writeEvent("BRISC", 1, 1, 110) + "," +
            zoneEvent("BRISC", 1, 1, "zone_b", "ZONE_END", 600) + "," +
            zoneEvent("NCRISC", 2, 1, "zone_c", "ZONE_START", 20) + "," +
            writeEvent("NCRISC", 2, 1, 120) + "," +
            zoneEvent("NCRISC", 2, 1, "zone_c", "ZONE_END", 500) + "]");
    ASSERT_TRUE(workload.has_value());
    EXPECT_EQ(
        zonePaths(*workload), (std::vector<std::string>{"zone_a[0]", "zone_b[0]", "zone_c[0]"}));
}

TEST(npeWorkloadTest, NestedZonesAreTrackedOverTime) {
    auto workload = ingestTrace(
        "nested_zones",
        "[" + zoneEvent("NCRISC", 1, 1, "outer", "ZONE_START", 0) + "," +
            zoneEvent("NCRISC", 1, 1, "inner", "ZONE_START", 10) + "," +
            writeEvent("NCRISC", 1, 1, 20) + "," +
            zoneEvent("NCRISC", 1, 1, "inner", "ZONE_END", 30) + "," +
            writeEvent("NCRISC", 1, 1, 40) + "," +
            zoneEvent("NCRISC", 1, 1, "inner", "ZONE_START", 50) + "," +
            writeEvent("NCRISC", 1, 1, 60) + "," +
            zoneEvent("NCRISC", 1, 1, "inner", "ZONE_END", 70) + "," +
            zoneEvent("NCRISC", 1, 1, "outer", "ZONE_END", 80) + "]");
    ASSERT_TRUE(workload.has_value());
    EXPECT_EQ(
        zonePaths(*workload),
        (std::vector<std::string>{"outer[0]/inner[0]", "outer[0]", "outer[0]/inner[1]"}));
}

TEST(npeWorkloadTest, IgnoresZoneEventsWithUnrecognizedProcOrPhase) {
    auto workload = ingestTrace(
        "unknown_zone_fields",
        "[" + zoneEvent("NOT_A_RISC", 1, 1, "bad_proc", "ZONE_START", 0) + "," +
            zoneEvent("NCRISC", 1, 1, "legacy_phase", "begin", 0) + "," +
            writeEvent("NCRISC", 1, 1, 100) + "," +
            zoneEvent("NCRISC", 1, 1, "legacy_phase", "end", 700) + "]");
    ASSERT_TRUE(workload.has_value());
    EXPECT_TRUE(workload->getZones().empty());
    EXPECT_EQ(zonePaths(*workload), (std::vector<std::string>{""}));
}

TEST(npeWorkloadTest, HandlesEventsWithEmptyOrUnrecognizedProc) {
    auto workload = ingestTrace(
        "unknown_event_proc",
        "[" + zoneEvent("", 1, 1, "zone_only", "ZONE_START", 0) + "," +
            writeEvent("", 1, 1, 100) + "," +
            writeEvent("NOT_A_RISC", 2, 1, 110) + "]");
    ASSERT_TRUE(workload.has_value());
    // events without proc are skipped (existing behaviour); events with an
    // unrecognized proc are still ingested, but without zone attribution
    EXPECT_EQ(zonePaths(*workload), (std::vector<std::string>{""}));
}

}  // namespace tt_npe
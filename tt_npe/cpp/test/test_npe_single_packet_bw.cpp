// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "device_models/blackhole.hpp"
#include "device_models/wormhole_b0.hpp"
#include "npeAPI.hpp"
#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "npeDeviceModelUtils.hpp"

namespace tt_npe {
namespace {

// Blackhole transfer bandwidth table (device_models/blackhole.hpp). Duplicated here so the tests
// document the exact table they reason about; asserted to match the model below.
const TransferBandwidthTable kBlackholeTBT = {
    {0, 0},
    {128, 6.0},
    {256, 12.1},
    {512, 24.2},
    {1024, 48.0},
    {2048, 57.7},
    {4096, 58.7},
    {8192, 60.4},
    {16384, 60.9}};

constexpr float kBlackholeMaxBW = 60.9f;

float legacyBW(size_t packet_size, size_t num_packets) {
    return interpolateBW(
        kBlackholeTBT, kBlackholeMaxBW, packet_size, num_packets, SinglePacketBWModel::Legacy);
}

float latencyFloorBW(size_t packet_size, size_t num_packets) {
    return interpolateBW(
        kBlackholeTBT,
        kBlackholeMaxBW,
        packet_size,
        num_packets,
        SinglePacketBWModel::LatencyFloor);
}

}  // namespace

// The table this test file reasons about must be the table the device model actually uses.
TEST(npeSinglePacketBWTest, TestTableMatchesBlackholeModel) {
    BlackholeDeviceModel model(BlackholeDeviceModel::DRAMHarvestingConfig::NO_HARVESTING);
    const auto &tbt = model.getTransferBandwidthTable();
    ASSERT_EQ(tbt.size(), kBlackholeTBT.size());
    for (size_t i = 0; i < tbt.size(); i++) {
        EXPECT_EQ(tbt[i].first, kBlackholeTBT[i].first);
        EXPECT_FLOAT_EQ(tbt[i].second, kBlackholeTBT[i].second);
    }
    EXPECT_FLOAT_EQ(model.getMaxNoCTransferBandwidth(), kBlackholeMaxBW);
}

// Default argument must preserve historical behavior for every caller that does not opt in.
TEST(npeSinglePacketBWTest, DefaultArgumentIsLegacy) {
    for (size_t packet_size : {16, 128, 512, 1024, 2048, 4096, 8192}) {
        for (size_t num_packets : {1, 2, 4, 17}) {
            EXPECT_FLOAT_EQ(
                interpolateBW(kBlackholeTBT, kBlackholeMaxBW, packet_size, num_packets),
                legacyBW(packet_size, num_packets))
                << "packet_size=" << packet_size << " num_packets=" << num_packets;
        }
    }
}

// The defect: with num_packets == 1 legacy returns peak bandwidth for *any* packet size.
TEST(npeSinglePacketBWTest, LegacySinglePacketIsSizeIndependent) {
    for (size_t packet_size : {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192}) {
        EXPECT_FLOAT_EQ(legacyBW(packet_size, 1), kBlackholeMaxBW)
            << "packet_size=" << packet_size;
    }
}

// latency_floor: a single packet is charged the size-appropriate table rate instead.
TEST(npeSinglePacketBWTest, LatencyFloorSinglePacketIsSizeDependent) {
    EXPECT_FLOAT_EQ(latencyFloorBW(128, 1), 6.0f);
    EXPECT_FLOAT_EQ(latencyFloorBW(512, 1), 24.2f);
    EXPECT_FLOAT_EQ(latencyFloorBW(1024, 1), 48.0f);
    EXPECT_FLOAT_EQ(latencyFloorBW(2048, 1), 57.7f);

    // strictly increasing with packet size below the knee
    float prev = 0.0f;
    for (size_t packet_size : {16, 32, 64, 128, 256, 512, 1024}) {
        float bw = latencyFloorBW(packet_size, 1);
        EXPECT_GT(bw, prev) << "packet_size=" << packet_size;
        prev = bw;
    }
}

// Below the knee the table is linear through the origin, so packet_size / bw -- the modelled cost
// of one transaction in cycles -- is a constant. That constant is the latency floor.
TEST(npeSinglePacketBWTest, LatencyFloorIsAConstantCyclesPerTransactionBelowKnee) {
    const float expected_cycles = 128.0f / 6.0f;  // ~21.33
    for (size_t packet_size : {16, 32, 64, 128, 256, 512, 1024}) {
        float cycles = float(packet_size) / latencyFloorBW(packet_size, 1);
        EXPECT_NEAR(cycles, expected_cycles, 0.35f) << "packet_size=" << packet_size;
    }
    // legacy charges ~0.26 cycles for a 16B transaction instead of ~21.3
    EXPECT_NEAR(16.0f / legacyBW(16, 1), 0.263f, 0.001f);
}

// A 16B and a 512B single packet must no longer be modelled at the same bandwidth.
TEST(npeSinglePacketBWTest, SubKneeRatioIsRestored) {
    EXPECT_FLOAT_EQ(legacyBW(16, 1), legacyBW(512, 1));

    float bw16 = latencyFloorBW(16, 1);
    float bw512 = latencyFloorBW(512, 1);
    EXPECT_LT(bw16, bw512);
    // 32x fewer transactions for the same payload => ~32x less modelled time
    EXPECT_NEAR(bw512 / bw16, 32.0f, 0.5f);
}

// Multi-packet behavior must be untouched in both modes.
TEST(npeSinglePacketBWTest, MultiPacketBehaviorIsUnchanged) {
    for (size_t packet_size : {16, 128, 512, 1024, 2048, 4096, 8192, 16384}) {
        for (size_t num_packets : {2, 3, 8, 64, 1000}) {
            EXPECT_FLOAT_EQ(
                latencyFloorBW(packet_size, num_packets), legacyBW(packet_size, num_packets))
                << "packet_size=" << packet_size << " num_packets=" << num_packets;
        }
    }
}

// Packet sizes at or above the largest table entry are unaffected in both modes (the early-return
// path below the interpolation loop).
TEST(npeSinglePacketBWTest, AboveTableIsUnchanged) {
    for (size_t packet_size : {16384, 32768, 1 << 20}) {
        EXPECT_FLOAT_EQ(latencyFloorBW(packet_size, 1), legacyBW(packet_size, 1))
            << "packet_size=" << packet_size;
    }
}

// The Wormhole table is flat from 2048B upward and its max equals that flat value, so the two modes
// agree exactly over the whole packet-size range the bundled trace corpus uses (2048B / 4096B).
TEST(npeSinglePacketBWTest, WormholeCorpusPacketSizesAreUnaffected) {
    WormholeB0DeviceModel model;
    const auto &tbt = model.getTransferBandwidthTable();
    float max_bw = model.getMaxNoCTransferBandwidth();
    for (size_t packet_size : {2048, 4096, 8192}) {
        EXPECT_FLOAT_EQ(
            interpolateBW(tbt, max_bw, packet_size, 1, SinglePacketBWModel::LatencyFloor),
            interpolateBW(tbt, max_bw, packet_size, 1, SinglePacketBWModel::Legacy))
            << "packet_size=" << packet_size;
    }
    // ... but sub-knee Wormhole packets do differ
    EXPECT_LT(
        interpolateBW(tbt, max_bw, 512, 1, SinglePacketBWModel::LatencyFloor),
        interpolateBW(tbt, max_bw, 512, 1, SinglePacketBWModel::Legacy));
}

TEST(npeSinglePacketBWTest, ConfigDefaultsToLegacy) {
    npeConfig cfg;
    EXPECT_EQ(cfg.single_packet_bandwidth_model, "legacy");
    EXPECT_EQ(cfg.getSinglePacketBWModel(), SinglePacketBWModel::Legacy);
}

TEST(npeSinglePacketBWTest, ConfigSelectsLatencyFloor) {
    npeConfig cfg;
    cfg.single_packet_bandwidth_model = "latency_floor";
    EXPECT_EQ(cfg.getSinglePacketBWModel(), SinglePacketBWModel::LatencyFloor);
    EXPECT_NO_THROW(npeAPI{cfg});
}

TEST(npeSinglePacketBWTest, ConfigRejectsUnknownModelName) {
    npeConfig cfg;
    cfg.single_packet_bandwidth_model = "not_a_model";
    EXPECT_THROW(npeAPI{cfg}, npeException);
}

// End-to-end through updateTransferBandwidth(), which is what the device models call.
TEST(npeSinglePacketBWTest, UpdateTransferBandwidthHonorsMode) {
    auto make_transfer = [](uint32_t packet_size, uint32_t num_packets) {
        npeWorkloadTransfer tr(
            packet_size,
            num_packets,
            Coord{0, 1, 1},
            Coord{0, 5, 5},
            /*injection_rate=*/1e9f,
            /*phase_cycle_offset=*/0,
            nocType::NOC0);
        return PETransferState(tr, 0, {});
    };

    std::vector<PETransferState> transfers = {make_transfer(16, 1), make_transfer(512, 1)};
    std::vector<PETransferID> live = {0, 1};

    updateTransferBandwidth(&transfers, live, kBlackholeTBT, kBlackholeMaxBW);
    EXPECT_FLOAT_EQ(transfers[0].curr_bandwidth, kBlackholeMaxBW);
    EXPECT_FLOAT_EQ(transfers[1].curr_bandwidth, kBlackholeMaxBW);

    updateTransferBandwidth(
        &transfers, live, kBlackholeTBT, kBlackholeMaxBW, SinglePacketBWModel::LatencyFloor);
    EXPECT_FLOAT_EQ(transfers[0].curr_bandwidth, latencyFloorBW(16, 1));
    EXPECT_FLOAT_EQ(transfers[1].curr_bandwidth, 24.2f);
}

}  // namespace tt_npe

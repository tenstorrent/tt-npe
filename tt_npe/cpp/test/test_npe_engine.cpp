// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "npeConfig.hpp"
#include "npeEngine.hpp"

namespace tt_npe {

TEST(npeEngineTest, CanConstructEngineForWormholeB0) {
    npeConfig cfg;
    cfg.device_name = "wormhole_b0";
    tt_npe::npeEngine engine(cfg);
}

TEST(npeEngineTest, CanRunSimpleWorkload) {
    npeConfig cfg;
    cfg.device_name = "wormhole_b0";
    tt_npe::npeEngine engine(cfg);
    auto device_id = 0;

    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(2048, 1, {device_id, 1, 1}, Coord{device_id, 1, 5}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    auto result = engine.runPerfEstimation(wl, cfg);
    EXPECT_TRUE(std::holds_alternative<npeStats>(result));
}

TEST(npeEngineTest, CanRunSimpleWorkloadCongestionFree) {
    npeConfig cfg;
    cfg.device_name = "wormhole_b0";
    cfg.congestion_model_name = "none";
    tt_npe::npeEngine engine(cfg);
    auto device_id = 0;

    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    phase.transfers.push_back(npeWorkloadTransfer(2048, 1, {device_id, 1, 1}, Coord{device_id, 1, 5}, 28.1, 0, nocType::NOC1));
    wl.addPhase(phase);

    auto result = engine.runPerfEstimation(wl, cfg);
    EXPECT_TRUE(std::holds_alternative<npeStats>(result));
}

TEST(npeEngineTest, CanTimeoutOnMaxCycles) {
    npeConfig cfg;
    cfg.device_name = "wormhole_b0";
    cfg.congestion_model_name = "none";
    cfg.cycles_per_timestep = 10000;
    tt_npe::npeEngine engine(cfg);
    auto device_id = 0;

    tt_npe::npeWorkload wl;
    tt_npe::npeWorkloadPhase phase;
    for (int i=0; i < 1000; i++) {
        phase.transfers.push_back(
            npeWorkloadTransfer(100000, 100000, {device_id, 1, 1}, Coord{device_id, 1, 5}, 28.1, 0, nocType::NOC1));
    }
    wl.addPhase(phase);

    auto result = engine.runPerfEstimation(wl, cfg);
    EXPECT_TRUE(std::holds_alternative<npeException>(result));
}

}  // namespace tt_npe
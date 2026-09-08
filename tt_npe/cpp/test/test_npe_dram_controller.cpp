// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

// Tests for the per-DRAM-controller congestion model (npeConfig::dram_controller_model).
//
// The model has three modes and the tests below are organized around the invariants each
// one must satisfy:
//   off     (default) : no grid is allocated; every new stat is inert and cycle counts are
//                       whatever they were before the feature existed.
//   observe           : demand is accumulated and reported but never derates, so cycle
//                       counts must be *identical* to off.
//   enforce           : demand additionally derates bandwidth, so an oversubscribed
//                       controller must lengthen the estimated runtime.

#include <algorithm>
#include <vector>

#include "device_models/blackhole.hpp"
#include "device_models/blackhole_multichip.hpp"
#include "device_models/wormhole_b0.hpp"
#include "device_models/wormhole_multichip.hpp"
#include "gtest/gtest.h"
#include "npeAPI.hpp"
#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "npeDeviceModelFactory.hpp"
#include "npeWorkload.hpp"

namespace tt_npe {
namespace {

//---- helpers ---------------------------------------------------------------------------

// Every DRAM coordinate known to a model, gathered by scanning the grid.
std::vector<Coord> collectDramCoords(const npeDeviceModel &model, DeviceID device_id) {
    std::vector<Coord> coords;
    for (int r = 0; r < int(model.getRows()); r++) {
        for (int c = 0; c < int(model.getCols()); c++) {
            Coord coord{device_id, r, c};
            if (model.getCoreType(coord) == CoreType::DRAM) {
                coords.push_back(coord);
            }
        }
    }
    return coords;
}

struct SrcDstPair {
    Coord src;
    Coord dst;
};

// Builds a workload of concurrent DRAM->WORKER reads, one transfer per pair, all starting
// at cycle 0 so they contend for the whole simulation.
npeWorkload makeDramReadWorkload(const std::vector<SrcDstPair> &pairs, uint32_t num_packets = 512) {
    npeWorkload wl;
    npeWorkloadPhase phase;
    for (const auto &p : pairs) {
        phase.transfers.push_back(
            npeWorkloadTransfer(
                8192,
                num_packets,
                p.src,
                p.dst,
                0.0f,  // injection rate is inferred from src core type
                0,
                nocType::NOC0));
    }
    wl.addPhase(phase);
    // golden cycles are only used for the post-hoc *_util reporting fields, not for the sim
    wl.setGoldenResultCycles({{0, {0, 32}}});
    return wl;
}

// Blackhole P150 controller 0 is shared by DRAM coords (0,0), (1,0) and (11,0). Each of
// those NIUs can inject 40 B/cyc while the controller behind them is also rated 40 B/cyc,
// so three concurrent reads demand 300% of the controller.
//
// Destinations are picked so the three NOC0 routes (east along the source row, then south)
// share no link and no sink NIU. The DRAM controller is therefore the *only* resource the
// three transfers contend for, which is what makes "enforce lengthens the run" attributable
// to the controller term alone.
std::vector<SrcDstPair> blackholeHotController0Pairs(DeviceID device_id = 0) {
    return {
        {Coord{device_id, 0, 0}, Coord{device_id, 2, 3}},
        {Coord{device_id, 1, 0}, Coord{device_id, 3, 4}},
        {Coord{device_id, 11, 0}, Coord{device_id, 11, 5}},
    };
}

// One DRAM source per controller, each reading into a worker in its own row. No controller
// sees more than a single NIU's worth of demand, so the controller term must stay inert.
std::vector<SrcDstPair> blackholeSpreadPairs() {
    return {
        {Coord{0, 11, 0}, Coord{0, 11, 3}},    // controller 0
        {Coord{0, 2, 0}, Coord{0, 2, 3}},      // controller 1
        {Coord{0, 9, 0}, Coord{0, 9, 3}},      // controller 2
        {Coord{0, 5, 0}, Coord{0, 5, 3}},      // controller 3
        {Coord{0, 11, 9}, Coord{0, 11, 12}},   // controller 4
        {Coord{0, 2, 9}, Coord{0, 2, 12}},     // controller 5
        {Coord{0, 9, 9}, Coord{0, 9, 12}},     // controller 6
        {Coord{0, 5, 9}, Coord{0, 5, 12}},     // controller 7
    };
}

npeConfig makeConfig(
    const std::string &device_name, const std::string &dram_model, float capacity_scale = 1.0f) {
    npeConfig cfg;
    cfg.device_name = device_name;
    cfg.congestion_model_name = "fast";
    cfg.cycles_per_timestep = 32;
    cfg.dram_controller_model = dram_model;
    cfg.dram_controller_capacity_scale = capacity_scale;
    return cfg;
}

npeStats runOrFail(const npeConfig &cfg, const npeWorkload &wl) {
    npeAPI api(cfg);
    auto result = api.runNPE(wl);
    EXPECT_TRUE(std::holds_alternative<npeStats>(result));
    return std::get<npeStats>(result);
}

const npeStats::deviceStats &meshStats(const npeStats &stats) {
    return stats.per_device_stats.at(MESH_DEVICE);
}

}  // namespace

//---- grid sizing / ID mapping -----------------------------------------------------------

TEST(npeDramControllerTest, BlackholeGridSizingCoversEveryControllerID) {
    BlackholeDeviceModel model(BlackholeDeviceModel::DRAMHarvestingConfig::NO_HARVESTING);

    EXPECT_EQ(model.getNumDramControllers(), 8u);
    EXPECT_EQ(model.getNumDramDemandSlots(), 8u);

    // getDRAMBandwidthPerChip() == NUM_DRAM_CONTROLLERS * per-controller BW, so this pins
    // the (private) aggregate-bandwidth constant at 8 for the unharvested part.
    EXPECT_NEAR(model.getDRAMBandwidthPerChip() / model.getDRAMBandwidthPerController(), 8.0, 1e-4);

    for (const auto &coord : collectDramCoords(model, model.getDeviceID())) {
        EXPECT_LT(model.getDramDemandID(coord), model.getNumDramDemandSlots())
            << "coord (" << coord.row << "," << coord.col << ") maps out of grid bounds";
    }
}

// The edge case the feature explicitly guards: under SINGLE_BANK_HARVESTING the model's
// NUM_DRAM_CONTROLLERS constant drops to 7, but dram_coord_to_controller_map still emits
// IDs 0..7. Sizing the demand grid off the constant would be an out-of-bounds write on
// controller 7, so getNumDramControllers() must report the coordinate map's 8.
TEST(npeDramControllerTest, BlackholeHarvestedGridSizingIsNotNumDramControllersConstant) {
    BlackholeDeviceModel harvested(
        BlackholeDeviceModel::DRAMHarvestingConfig::SINGLE_BANK_HARVESTING);

    // aggregate-bandwidth constant really is 7 under harvesting ...
    EXPECT_NEAR(
        harvested.getDRAMBandwidthPerChip() / harvested.getDRAMBandwidthPerController(),
        7.0,
        1e-4);
    // ... while the coordinate map still emits controller ID 7, so the grid must hold 8.
    EXPECT_EQ(harvested.getDramControllerIDForCore(Coord{harvested.getDeviceID(), 5, 9}), 7u);
    EXPECT_EQ(harvested.getNumDramControllers(), 8u);
    EXPECT_EQ(harvested.getNumDramDemandSlots(), 8u);

    for (const auto &coord : collectDramCoords(harvested, harvested.getDeviceID())) {
        EXPECT_LT(harvested.getDramDemandID(coord), harvested.getNumDramDemandSlots());
    }
}

TEST(npeDramControllerTest, WormholeGridSizingCoversEveryControllerID) {
    WormholeB0DeviceModel model;

    EXPECT_EQ(model.getNumDramControllers(), 6u);
    EXPECT_EQ(model.getNumDramDemandSlots(), 6u);
    for (const auto &coord : collectDramCoords(model, model.getDeviceID())) {
        EXPECT_LT(model.getDramDemandID(coord), model.getNumDramDemandSlots());
    }
}

// getDramControllerIDForCore() on the multichip models delegates to their single-chip
// member, whose coordinate map hardcodes device_id 0. Without the explicit device_id stride
// in getDramDemandID() every chip would alias onto chip 0's slots.
template <typename ModelT>
void checkMultichipDemandIDsDoNotCollide(const ModelT &model) {
    const size_t per_chip = model.getNumDramControllers();
    ASSERT_GT(per_chip, 0u);
    EXPECT_EQ(model.getNumDramDemandSlots(), model.getNumChips() * per_chip);

    std::vector<bool> slot_seen(model.getNumDramDemandSlots(), false);
    for (size_t dev = 0; dev < model.getNumChips(); dev++) {
        const auto device_id = DeviceID(dev);
        auto dram_coords = collectDramCoords(model, device_id);
        ASSERT_FALSE(dram_coords.empty());
        for (const auto &coord : dram_coords) {
            size_t slot = model.getDramDemandID(coord);
            ASSERT_LT(slot, model.getNumDramDemandSlots());
            // slot must live in this chip's band
            EXPECT_EQ(slot / per_chip, dev) << "controller ID collided across chips";
            // the same (row,col) on chip N must be exactly N bands above chip 0
            EXPECT_EQ(
                slot,
                dev * per_chip + model.getDramDemandID(Coord{DeviceID(0), coord.row, coord.col}));
            slot_seen[slot] = true;
        }
    }
    EXPECT_TRUE(std::all_of(slot_seen.begin(), slot_seen.end(), [](bool b) { return b; }))
        << "some demand grid slots are unreachable from any DRAM coord";
}

TEST(npeDramControllerTest, WormholeMultichipDemandIDsDoNotCollideAcrossChips) {
    WormholeMultichipDeviceModel model(4);
    checkMultichipDemandIDsDoNotCollide(model);
}

TEST(npeDramControllerTest, BlackholeMultichipDemandIDsDoNotCollideAcrossChips) {
    BlackholeMultichipDeviceModel model(2);
    checkMultichipDemandIDsDoNotCollide(model);
}

// initDeviceState() defaults to *not* allocating the grid; an empty grid is what
// short-circuits every new code path inside modelCongestion.
TEST(npeDramControllerTest, InitDeviceStateOnlyAllocatesGridWhenRequested) {
    std::vector<std::string> device_names = {"wormhole_b0", "P150", "blackhole", "T3K", "P300"};
    for (const auto &name : device_names) {
        auto model = npeDeviceModelFactory::createDeviceModel(name);

        auto default_state = model->initDeviceState();
        EXPECT_TRUE(default_state->getDramDemandGrid().empty()) << "device " << name;

        auto disabled_state = model->initDeviceState(false);
        EXPECT_TRUE(disabled_state->getDramDemandGrid().empty()) << "device " << name;

        auto enabled_state = model->initDeviceState(true);
        EXPECT_EQ(enabled_state->getDramDemandGrid().size(), model->getNumDramDemandSlots())
            << "device " << name;
    }
}

//---- config plumbing --------------------------------------------------------------------

TEST(npeDramControllerTest, ConfigTranslatesTriStateFlag) {
    npeConfig cfg;
    EXPECT_EQ(cfg.dram_controller_model, "off");
    EXPECT_FALSE(cfg.getDramCongestionParams().enabled());
    EXPECT_FALSE(cfg.getDramCongestionParams().enforcing());

    cfg.dram_controller_model = "observe";
    EXPECT_TRUE(cfg.getDramCongestionParams().enabled());
    EXPECT_FALSE(cfg.getDramCongestionParams().enforcing());

    cfg.dram_controller_model = "enforce";
    EXPECT_TRUE(cfg.getDramCongestionParams().enabled());
    EXPECT_TRUE(cfg.getDramCongestionParams().enforcing());

    cfg.dram_controller_capacity_scale = 2.5f;
    EXPECT_FLOAT_EQ(cfg.getDramCongestionParams().capacity_scale, 2.5f);
}

TEST(npeDramControllerTest, RejectsInvalidDramControllerModel) {
    // a typo must be a hard error rather than a silently disabled feature
    EXPECT_THROW(npeAPI api(makeConfig("P150", "enfroce")), npeException);
    EXPECT_THROW(npeAPI api(makeConfig("P150", "")), npeException);
    EXPECT_NO_THROW(npeAPI api(makeConfig("P150", "off")));
    EXPECT_NO_THROW(npeAPI api(makeConfig("P150", "observe")));
    EXPECT_NO_THROW(npeAPI api(makeConfig("P150", "enforce")));
}

TEST(npeDramControllerTest, RejectsNonPositiveCapacityScale) {
    EXPECT_THROW(npeAPI api(makeConfig("P150", "enforce", 0.0f)), npeException);
    EXPECT_THROW(npeAPI api(makeConfig("P150", "enforce", -1.0f)), npeException);
}

//---- end-to-end behavior ----------------------------------------------------------------

// (a) default-off back-compat: none of the new state exists unless asked for.
TEST(npeDramControllerTest, DefaultOffLeavesAllNewStatsInert) {
    auto wl = makeDramReadWorkload(blackholeHotController0Pairs());

    npeConfig cfg;
    cfg.device_name = "P150";
    cfg.cycles_per_timestep = 32;
    // deliberately *not* touching dram_controller_model -- this is the shipped default
    ASSERT_EQ(cfg.dram_controller_model, "off");

    auto stats = runOrFail(cfg, wl);
    const auto &ds = meshStats(stats);

    EXPECT_DOUBLE_EQ(ds.dram_controller_capacity, 0.0);
    EXPECT_DOUBLE_EQ(ds.overall_max_dram_controller_demand, 0.0);
    EXPECT_DOUBLE_EQ(ds.overall_avg_dram_controller_demand, 0.0);
    EXPECT_TRUE(ds.dram_controller_peak_demand.empty());
    EXPECT_TRUE(ds.dram_controller_mean_demand.empty());
    EXPECT_TRUE(ds.dram_controller_saturated_frac.empty());
    EXPECT_EQ(ds.dram_num_controllers, 0u);
    EXPECT_TRUE(ds.getDRAMHotspots().empty());
    EXPECT_EQ(ds.getDRAMHotspotStr(), "-");
    EXPECT_FALSE(ds.isDRAMBound());

    // the per-timestep grid is never allocated either
    ASSERT_FALSE(ds.per_timestep_stats.empty());
    for (const auto &ts : ds.per_timestep_stats) {
        EXPECT_TRUE(ts.dram_demand_grid.empty());
    }

    // and the post-hoc DRAM reporting that predates this feature is still produced
    EXPECT_FALSE(ds.dram_bw_util_per_controller.empty());
}

// (b) observe accumulates and reports but must not move a single cycle.
TEST(npeDramControllerTest, ObserveReportsWithoutChangingCycles) {
    auto wl = makeDramReadWorkload(blackholeHotController0Pairs());

    auto off = runOrFail(makeConfig("P150", "off"), wl);
    auto observe = runOrFail(makeConfig("P150", "observe"), wl);

    EXPECT_EQ(meshStats(observe).estimated_cycles, meshStats(off).estimated_cycles);
    EXPECT_DOUBLE_EQ(meshStats(observe).dram_bw_util_sim, meshStats(off).dram_bw_util_sim);

    const auto &ds = meshStats(observe);
    // 40 B/cyc per controller on Blackhole; the scale factor is 1.0 here
    EXPECT_GT(ds.dram_controller_capacity, 0.0);
    ASSERT_TRUE(ds.dram_controller_peak_demand.contains(0u));
    // three 40 B/cyc DRAM NIUs behind one 40 B/cyc controller
    EXPECT_GT(ds.dram_controller_peak_demand.at(0u), 100.0);
    EXPECT_GT(ds.overall_max_dram_controller_demand, 100.0);
    EXPECT_TRUE(ds.isDRAMBound());

    auto hotspots = ds.getDRAMHotspots();
    ASSERT_FALSE(hotspots.empty());
    EXPECT_EQ(hotspots.front().controller_id, 0u);
    EXPECT_GT(hotspots.front().peak_demand_pct, 100.0);
    EXPECT_NE(ds.getDRAMHotspotStr(), "-");

    // controllers that saw no traffic must not be reported as hotspots
    for (const auto &hs : hotspots) {
        EXPECT_EQ(hs.controller_id, 0u) << "unexpected hotspot on an idle controller";
    }
}

// (c) enforce must actually derate an oversubscribed controller.
TEST(npeDramControllerTest, EnforceDeratesOversubscribedController) {
    auto wl = makeDramReadWorkload(blackholeHotController0Pairs());

    auto off = runOrFail(makeConfig("P150", "off"), wl);
    auto observe = runOrFail(makeConfig("P150", "observe"), wl);
    auto enforce = runOrFail(makeConfig("P150", "enforce"), wl);

    const auto &off_ds = meshStats(off);
    const auto &obs_ds = meshStats(observe);
    const auto &en_ds = meshStats(enforce);

    EXPECT_GT(en_ds.estimated_cycles, off_ds.estimated_cycles)
        << "enforce did not derate a 3x oversubscribed DRAM controller";

    // Three 40 B/cyc NIUs behind one 40 B/cyc controller: the run should stretch by roughly
    // 3x, which is the whole point of modelling the controller.
    const double stretch = double(en_ds.estimated_cycles) / double(off_ds.estimated_cycles);
    EXPECT_NEAR(stretch, 3.0, 0.35) << "observed stretch factor " << stretch;

    // The reported demand is *offered* demand: like the pre-existing link and NIU demand
    // grids it is sampled from the un-derated bandwidths at the top of each timestep, so it
    // reads the same 3x in observe and enforce. The difference between the modes shows up in
    // estimated_cycles, not in the demand numbers.
    ASSERT_TRUE(en_ds.dram_controller_peak_demand.contains(0u));
    ASSERT_TRUE(obs_ds.dram_controller_peak_demand.contains(0u));
    ASSERT_TRUE(en_ds.dram_controller_mean_demand.contains(0u));
    EXPECT_GT(en_ds.dram_controller_peak_demand.at(0u), 100.0);
    EXPECT_GT(en_ds.dram_controller_mean_demand.at(0u), 100.0);
    EXPECT_DOUBLE_EQ(
        en_ds.dram_controller_peak_demand.at(0u), obs_ds.dram_controller_peak_demand.at(0u));

    auto hotspots = en_ds.getDRAMHotspots();
    ASSERT_FALSE(hotspots.empty());
    EXPECT_EQ(hotspots.front().controller_id, 0u);
    EXPECT_EQ(hotspots.front().device_id, 0);
    EXPECT_GT(hotspots.front().saturated_frac, 0.5);
    EXPECT_GT(hotspots.front().saturated_cycles, 0.0);
    EXPECT_TRUE(en_ds.isDRAMBound());
}

// The controller term is the *only* thing separating enforce from off: scale the capacity
// far past anything the workload can demand and enforce must collapse back onto off.
TEST(npeDramControllerTest, EnforceWithUnreachableCapacityMatchesOff) {
    auto wl = makeDramReadWorkload(blackholeHotController0Pairs());

    auto off = runOrFail(makeConfig("P150", "off"), wl);
    auto enforce_loose = runOrFail(makeConfig("P150", "enforce", 8.0f), wl);

    EXPECT_EQ(meshStats(enforce_loose).estimated_cycles, meshStats(off).estimated_cycles);
    // reporting is still produced, just scaled against the inflated capacity
    EXPECT_GT(meshStats(enforce_loose).dram_controller_capacity, 0.0);
    EXPECT_LT(meshStats(enforce_loose).overall_max_dram_controller_demand, 100.0);
}

// A workload that spreads its DRAM traffic one NIU per controller leaves every controller
// at or below capacity, so enforce must not lengthen it.
TEST(npeDramControllerTest, EnforceDoesNotPenalizeSpreadDramTraffic) {
    auto wl = makeDramReadWorkload(blackholeSpreadPairs());

    auto off = runOrFail(makeConfig("P150", "off"), wl);
    auto observe = runOrFail(makeConfig("P150", "observe"), wl);
    auto enforce = runOrFail(makeConfig("P150", "enforce"), wl);

    // no controller is oversubscribed
    EXPECT_LE(meshStats(observe).overall_max_dram_controller_demand, 101.0);
    EXPECT_TRUE(meshStats(observe).getDRAMHotspots(150.0).empty());

    EXPECT_EQ(meshStats(enforce).estimated_cycles, meshStats(off).estimated_cycles);
}

// (d) regression for the Blackhole SINGLE_BANK_HARVESTING out-of-bounds case: "blackhole" /
// "P100" build the harvested model whose NUM_DRAM_CONTROLLERS is 7, while controller ID 7 is
// still reachable from coords (5,9)/(6,9)/(7,9). A grid sized 7 would be written past its
// end here.
TEST(npeDramControllerTest, HarvestedBlackholeHandlesHighestControllerID) {
    std::vector<SrcDstPair> pairs = {
        {Coord{0, 5, 9}, Coord{0, 5, 12}},
        {Coord{0, 6, 9}, Coord{0, 6, 12}},
        {Coord{0, 7, 9}, Coord{0, 7, 12}},
    };
    auto wl = makeDramReadWorkload(pairs);

    auto off = runOrFail(makeConfig("blackhole", "off"), wl);
    auto enforce = runOrFail(makeConfig("blackhole", "enforce"), wl);

    const auto &en_ds = meshStats(enforce);
    ASSERT_TRUE(en_ds.dram_controller_peak_demand.contains(7u))
        << "controller 7 never appeared in the demand grid";
    EXPECT_GT(en_ds.dram_controller_peak_demand.at(7u), 100.0);
    EXPECT_GT(en_ds.estimated_cycles, meshStats(off).estimated_cycles);

    auto hotspots = en_ds.getDRAMHotspots();
    ASSERT_FALSE(hotspots.empty());
    EXPECT_EQ(hotspots.front().controller_id, 7u);
}

// (d) multichip: hot controller 0 lives on chip 1, so the MESH_DEVICE grid key must be
// (1 * num_controllers + 0) and the hotspot must decompose back to device 1 / controller 0.
TEST(npeDramControllerTest, MultichipHotspotIsAttributedToTheRightChip) {
    constexpr DeviceID kHotDevice = 1;
    auto wl = makeDramReadWorkload(blackholeHotController0Pairs(kHotDevice));
    // every device the model knows about needs a golden cycle entry
    wl.setGoldenResultCycles({{0, {0, 32}}, {kHotDevice, {0, 32}}});

    auto off = runOrFail(makeConfig("P300", "off"), wl);
    auto enforce = runOrFail(makeConfig("P300", "enforce"), wl);
    EXPECT_GT(meshStats(enforce).estimated_cycles, meshStats(off).estimated_cycles);

    npeAPI api(makeConfig("P300", "enforce"));
    const size_t num_controllers = api.getDeviceModel().getNumDramControllers();

    // mesh-level view: keys are flattened grid slots
    const auto &mesh = meshStats(enforce);
    const uint32_t expected_slot = uint32_t(kHotDevice * num_controllers + 0);
    ASSERT_TRUE(mesh.dram_controller_peak_demand.contains(expected_slot));
    EXPECT_GT(mesh.dram_controller_peak_demand.at(expected_slot), 100.0);
    // chip 0's controller 0 slot exists but must be idle -- if the device_id stride were
    // missing, chip 1's traffic would have landed here instead
    ASSERT_TRUE(mesh.dram_controller_peak_demand.contains(0u));
    EXPECT_DOUBLE_EQ(mesh.dram_controller_peak_demand.at(0u), 0.0)
        << "chip 0 controller 0 saw traffic that belongs to chip 1";

    auto mesh_hotspots = mesh.getDRAMHotspots();
    ASSERT_FALSE(mesh_hotspots.empty());
    EXPECT_EQ(mesh_hotspots.front().device_id, kHotDevice);
    EXPECT_EQ(mesh_hotspots.front().controller_id, 0u);

    // per-device view: keys are plain controller IDs
    ASSERT_TRUE(enforce.per_device_stats.contains(kHotDevice));
    const auto &dev = enforce.per_device_stats.at(kHotDevice);
    ASSERT_TRUE(dev.dram_controller_peak_demand.contains(0u));
    EXPECT_GT(dev.dram_controller_peak_demand.at(0u), 100.0);
    auto dev_hotspots = dev.getDRAMHotspots();
    ASSERT_FALSE(dev_hotspots.empty());
    EXPECT_EQ(dev_hotspots.front().device_id, kHotDevice);
    EXPECT_EQ(dev_hotspots.front().controller_id, 0u);
}

// The DRAM controller model is a refinement of the congestion model; with congestion
// modelling switched off entirely it must stay inert.
TEST(npeDramControllerTest, InertWhenBaseCongestionModelIsDisabled) {
    auto wl = makeDramReadWorkload(blackholeHotController0Pairs());

    auto cfg = makeConfig("P150", "enforce");
    cfg.congestion_model_name = "none";
    auto no_cong_enforce = runOrFail(cfg, wl);

    cfg.dram_controller_model = "off";
    auto no_cong_off = runOrFail(cfg, wl);

    EXPECT_EQ(
        meshStats(no_cong_enforce).estimated_cycles, meshStats(no_cong_off).estimated_cycles);
    EXPECT_DOUBLE_EQ(meshStats(no_cong_enforce).dram_controller_capacity, 0.0);
    EXPECT_TRUE(meshStats(no_cong_enforce).dram_controller_peak_demand.empty());
}

// Wormhole carries the same feature through a separate copy of modelCongestion; controller 0
// there is shared by (0,0), (1,0) and (11,0).
TEST(npeDramControllerTest, WormholeEnforceDeratesOversubscribedController) {
    std::vector<SrcDstPair> pairs = {
        {Coord{0, 0, 0}, Coord{0, 2, 3}},
        {Coord{0, 1, 0}, Coord{0, 3, 4}},
        {Coord{0, 11, 0}, Coord{0, 11, 6}},
    };
    auto wl = makeDramReadWorkload(pairs);

    auto off = runOrFail(makeConfig("wormhole_b0", "off"), wl);
    auto observe = runOrFail(makeConfig("wormhole_b0", "observe"), wl);
    auto enforce = runOrFail(makeConfig("wormhole_b0", "enforce"), wl);

    EXPECT_EQ(meshStats(observe).estimated_cycles, meshStats(off).estimated_cycles);
    EXPECT_GT(meshStats(observe).overall_max_dram_controller_demand, 100.0);
    EXPECT_GT(meshStats(enforce).estimated_cycles, meshStats(off).estimated_cycles);
}

}  // namespace tt_npe

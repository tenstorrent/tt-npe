# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import tt_npe_pybind as npe
import pytest
import random
import pprint


def test_npe_init():
    npe_api = npe.InitAPI(npe.Config())
    assert npe_api is not None

def test_npe_invalid_init():
    cfg = npe.Config()
    cfg.device_name = "dne"
    assert npe.InitAPI(cfg) is None


def test_npe_can_print_exception():
    assert npe.Exception.__repr__ is not object.__repr__


def test_npe_can_get_exception_str():
    assert npe.Exception.__str__ is not object.__str__


def test_npe_can_change_config_fields():
    cfg = npe.Config()
    cfg.device_name = "wormhole_b0"
    cfg.congestion_model_name = "none"
    cfg.workload_json_filepath = "workload/example_wl.json"
    cfg.cycles_per_timestep = 64
    cfg.set_verbosity_level(2)
    npe_api = npe.InitAPI(cfg)
    assert npe_api is not None


def test_npe_can_print_stats():
    assert npe.Stats.__str__ is not object.__str__


def test_npe_run_workload():
    wl = npe.createWorkloadFromJSON("workload/example_wl.json", "wormhole_b0")
    npe_api = npe.InitAPI(npe.Config())
    assert npe_api is not None
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats

def test_npe_max_cycle_limit():
    wl = npe.Workload()
    phase = npe.Phase()
    for i in range(1000):
        phase.addTransfer(
            npe.Transfer(
                200000, 20000, npe.Coord(0, 1, 1), npe.Coord(0, 1, 5), 0.0, 0, npe.NocType.NOC_1
            )
        ) 
    wl.addPhase(phase)
    # set dummy value for golden cycles
    wl.setGoldenResultCycles({0: (0, 32)})
    cfg = npe.Config()
    cfg.cycles_per_timestep = 100000
    npe_api = npe.InitAPI(cfg)
    assert npe_api is not None
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Exception


def test_npe_create_and_run_synthetic_workload():
    transfer = npe.Transfer(
        2048, 10, npe.Coord(0, 1, 1), npe.Coord(0, 1, 5), 0.0, 0, npe.NocType.NOC_0
    )

    phase = npe.Phase()
    phase.addTransfer(transfer)

    wl = npe.Workload()
    wl.addPhase(phase)
    # set dummy value for golden cycles
    wl.setGoldenResultCycles({0: (0, 32)})

    npe_api = npe.InitAPI(npe.Config())
    assert npe_api is not None
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats


def test_npe_create_and_run_larger_synthetic_workload():

    phase = npe.Phase()
    for i in range(64):
        ps = random.choice(range(2, 16)) * 256
        np = random.choice(range(2, 10))
        sx = random.choice(range(1, 8))
        sy = random.choice(range(1, 8))
        dx = random.choice(range(1, 8))
        dy = random.choice(range(1, 8))
        noc_type = random.choice([npe.NocType.NOC_0, npe.NocType.NOC_1])
        transfer = npe.Transfer(
            ps, np, npe.Coord(0, sy, sx), npe.Coord(0, dy, dx), 0.0, 0, noc_type
        )
        phase.addTransfer(transfer)

    wl = npe.Workload()
    wl.addPhase(phase)
    # set dummy value for golden cycles
    wl.setGoldenResultCycles({0: (0, 32)})

    npe_api = npe.InitAPI(npe.Config())
    assert npe_api is not None
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats


#---- per-DRAM-controller congestion model -------------------------------------------

# Blackhole P150 controller 0 is shared by DRAM coords (0,0), (1,0) and (11,0). Reading
# from all three at once demands 3x the controller's bandwidth while every individual NIU
# still looks unsaturated -- exactly what the per-controller model exists to catch.
BH_HOT_CONTROLLER_PAIRS = [
    ((0, 0), (2, 3)),
    ((1, 0), (3, 4)),
    ((11, 0), (11, 5)),
]


def _make_dram_hot_workload():
    phase = npe.Phase()
    for (src_row, src_col), (dst_row, dst_col) in BH_HOT_CONTROLLER_PAIRS:
        phase.addTransfer(
            npe.Transfer(
                8192,
                512,
                npe.Coord(0, src_row, src_col),
                npe.Coord(0, dst_row, dst_col),
                0.0,
                0,
                npe.NocType.NOC_0,
            )
        )
    wl = npe.Workload()
    wl.addPhase(phase)
    wl.setGoldenResultCycles({0: (0, 32)})
    return wl


def _make_dram_config(dram_controller_model, capacity_scale=1.0):
    cfg = npe.Config()
    cfg.device_name = "P150"
    cfg.cycles_per_timestep = 32
    cfg.dram_controller_model = dram_controller_model
    cfg.dram_controller_capacity_scale = capacity_scale
    return cfg


def _run(cfg, wl):
    npe_api = npe.InitAPI(cfg)
    assert npe_api is not None
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats
    # MESH_DEVICE is keyed as -1
    return result.per_device_stats[-1]


def test_npe_dram_controller_config_fields():
    cfg = npe.Config()
    # default is "off" so existing predictions are unchanged
    assert cfg.dram_controller_model == "off"
    assert cfg.dram_controller_capacity_scale == 1.0

    for mode in ("off", "observe", "enforce"):
        cfg.dram_controller_model = mode
        assert cfg.dram_controller_model == mode
        assert npe.InitAPI(cfg) is not None

    cfg.dram_controller_capacity_scale = 1.2
    assert cfg.dram_controller_capacity_scale == pytest.approx(1.2)


def test_npe_dram_controller_rejects_bad_config():
    cfg = _make_dram_config("enfroce")
    assert npe.InitAPI(cfg) is None

    cfg = _make_dram_config("enforce", capacity_scale=0.0)
    assert npe.InitAPI(cfg) is None


def test_npe_dram_controller_default_off_is_inert():
    stats = _run(_make_dram_config("off"), _make_dram_hot_workload())

    assert stats.dram_controller_capacity == 0.0
    assert stats.overall_max_dram_controller_demand == 0.0
    assert stats.overall_avg_dram_controller_demand == 0.0
    assert stats.dram_controller_peak_demand == {}
    assert stats.dram_controller_mean_demand == {}
    assert stats.dram_controller_saturated_frac == {}
    assert stats.getDRAMHotspots() == []
    assert stats.getDRAMHotspotStr() == "-"
    assert stats.isDRAMBound() is False


def test_npe_dram_controller_observe_reports_without_derating():
    wl = _make_dram_hot_workload()
    off = _run(_make_dram_config("off"), wl)
    observe = _run(_make_dram_config("observe"), wl)

    # observe accumulates and reports but must never move a cycle
    assert observe.estimated_cycles == off.estimated_cycles

    assert observe.dram_controller_capacity > 0.0
    assert isinstance(observe.dram_controller_peak_demand, dict)
    assert 0 in observe.dram_controller_peak_demand
    assert observe.dram_controller_peak_demand[0] > 100.0
    assert observe.overall_max_dram_controller_demand > 100.0
    assert observe.isDRAMBound() is True

    hotspots = observe.getDRAMHotspots()
    assert len(hotspots) > 0
    hs = hotspots[0]
    assert isinstance(hs, npe.DramHotspot)
    assert hs.controller_id == 0
    assert hs.device_id == 0
    assert hs.peak_demand_pct > 100.0
    assert hs.mean_demand_pct > 100.0
    assert 0.0 <= hs.saturated_frac <= 1.0
    assert hs.saturated_cycles > 0.0
    assert isinstance(str(hs), str) and str(hs) != ""

    # threshold argument filters the list
    assert observe.getDRAMHotspots(threshold_pct=1e6) == []
    assert observe.getDRAMHotspotStr() != "-"


def test_npe_dram_controller_enforce_derates():
    wl = _make_dram_hot_workload()
    off = _run(_make_dram_config("off"), wl)
    enforce = _run(_make_dram_config("enforce"), wl)

    assert enforce.estimated_cycles > off.estimated_cycles

    # the capacity scale is a calibration knob for the congestion model only; scaling it
    # past anything the workload can demand collapses enforce back onto off
    loose = _run(_make_dram_config("enforce", capacity_scale=8.0), wl)
    assert loose.estimated_cycles == off.estimated_cycles
    # ... while post-hoc DRAM BW reporting is untouched by the scale factor
    assert loose.dram_bw_util_per_controller == off.dram_bw_util_per_controller


def test_npe_dram_controller_stats_survive_pickling():
    import pickle

    observe = _run(_make_dram_config("observe"), _make_dram_hot_workload())
    restored = pickle.loads(pickle.dumps(observe))

    assert restored.dram_controller_capacity == observe.dram_controller_capacity
    assert restored.overall_max_dram_controller_demand == pytest.approx(
        observe.overall_max_dram_controller_demand
    )
    assert restored.dram_controller_peak_demand == observe.dram_controller_peak_demand
    assert restored.dram_controller_mean_demand == observe.dram_controller_mean_demand
    assert restored.dram_controller_saturated_frac == observe.dram_controller_saturated_frac

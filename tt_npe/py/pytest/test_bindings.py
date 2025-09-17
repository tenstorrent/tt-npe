# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import tt_npe_pybind as npe
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
    for i in range(100):
        phase.addTransfer(
            npe.Transfer(
                200000, 50000, npe.Coord(0, 1, 1), npe.Coord(0, 1, 5), 0.0, 0, npe.NocType.NOC_0
            )
        ) 
    wl.addPhase(phase)
    cfg = npe.Config()
    cfg.cycles_per_timestep = 10000
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

    npe_api = npe.InitAPI(npe.Config())
    assert npe_api is not None
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats

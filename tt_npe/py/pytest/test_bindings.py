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


def _single_transfer_cycles(packet_size, num_packets, mode, device="blackhole"):
    """Run one isolated transfer and return the predicted cycle count."""
    cfg = npe.Config()
    cfg.device_name = device
    cfg.single_packet_bandwidth_model = mode
    # NOTE: keep cycles_per_timestep at the default-ish 32. The engine truncates
    # `cycles_per_timestep * bandwidth` to an integer byte count, so a very small
    # cycles_per_timestep combined with a sub-1-B/cycle bandwidth makes no forward
    # progress. This is pre-existing engine behavior, reachable in "legacy" mode too.
    cfg.cycles_per_timestep = 32
    cfg.set_verbosity_level(0)

    phase = npe.Phase()
    phase.addTransfer(
        npe.Transfer(
            packet_size, num_packets, npe.Coord(0, 1, 1), npe.Coord(0, 5, 5), 0.0, 0, npe.NocType.NOC_0
        )
    )
    wl = npe.Workload()
    wl.addPhase(phase)
    wl.setGoldenResultCycles({0: (0, 100), -1: (0, 100)})

    npe_api = npe.InitAPI(cfg)
    assert npe_api is not None
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats
    return result.per_device_stats[-1].estimated_cycles


def test_npe_single_packet_bandwidth_model_defaults_to_legacy():
    cfg = npe.Config()
    assert cfg.single_packet_bandwidth_model == "legacy"


def test_npe_single_packet_bandwidth_model_rejects_unknown_value():
    cfg = npe.Config()
    cfg.single_packet_bandwidth_model = "not_a_model"
    assert npe.InitAPI(cfg) is None


def test_npe_single_packet_bandwidth_model_accepts_latency_floor():
    cfg = npe.Config()
    cfg.single_packet_bandwidth_model = "latency_floor"
    assert npe.InitAPI(cfg) is not None


def test_npe_legacy_single_packet_bandwidth_is_size_independent():
    # the defect: with num_packets == 1, legacy charges peak bandwidth for any packet
    # size, so predicted cycles scale linearly with size (constant bandwidth). The exact
    # bandwidth equality is asserted in the gtest; here it is only visible up to the
    # engine's integer cycle quantization.
    bw = {ps: ps / _single_transfer_cycles(ps, 1, "legacy") for ps in (256, 512, 1024)}
    assert max(bw.values()) / min(bw.values()) < 1.2


def test_npe_latency_floor_single_packet_is_size_dependent():
    # under latency_floor each sub-knee single-packet transaction costs the same
    # fixed number of cycles, so bandwidth scales with packet size
    cycles = {ps: _single_transfer_cycles(ps, 1, "latency_floor") for ps in (16, 512, 1024)}
    assert cycles[16] == cycles[512] == cycles[1024]
    bw16 = 16 / cycles[16]
    bw512 = 512 / cycles[512]
    assert bw512 > bw16
    assert 30.0 < bw512 / bw16 < 34.0  # ~32x, matching the 32x size ratio


def test_npe_latency_floor_is_slower_than_legacy_for_small_single_packets():
    for ps in (16, 128, 512):
        assert _single_transfer_cycles(ps, 1, "latency_floor") > _single_transfer_cycles(
            ps, 1, "legacy"
        )


def test_npe_multi_packet_transfers_are_unchanged_by_mode():
    for ps in (16, 512, 2048):
        for num_packets in (2, 4, 8):
            assert _single_transfer_cycles(ps, num_packets, "legacy") == _single_transfer_cycles(
                ps, num_packets, "latency_floor"
            )


def test_npe_wormhole_corpus_packet_sizes_are_unchanged_by_mode():
    # the wormhole table is flat at its max from 2048B up, so both modes agree over the
    # packet sizes the bundled noc trace corpus actually contains
    for ps in (2048, 4096):
        assert _single_transfer_cycles(
            ps, 1, "legacy", device="wormhole_b0"
        ) == _single_transfer_cycles(ps, 1, "latency_floor", device="wormhole_b0")


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

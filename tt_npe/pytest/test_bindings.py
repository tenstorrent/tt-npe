import tt_npe_pybind as npe
import random

def test_npe_init():
    npe_api = npe.API(npe.Config())

def test_npe_run_workload():
    wl = npe.createWorkloadFromYAML("workload/example.yaml")
    npe_api = npe.API(npe.Config())
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats

def test_npe_create_and_run_synthetic_workload():
    transfer = npe.createTransfer(
        2048, 10, npe.Coord(1, 1), npe.Coord(1, 5), 0.0, 0, npe.NocType.NOC_0
    )

    phase = npe.Phase()
    phase.addTransfer(transfer)

    wl = npe.Workload()
    wl.addPhase(phase)

    npe_api = npe.API(npe.Config())
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats

def test_npe_create_and_run_larger_synthetic_workload():

    phase = npe.Phase()
    for i in range(64):
        ps = random.choice(range(2,16)) * 256
        np = random.choice(range(2,10))
        sx = random.choice(range(1,8))
        sy = random.choice(range(1,8))
        dx = random.choice(range(1,8))
        dy = random.choice(range(1,8))
        noc_type = random.choice([npe.NocType.NOC_0,npe.NocType.NOC_1])
        transfer = npe.createTransfer(
            ps, np, npe.Coord(sy, sx), npe.Coord(dy, dx), 0.0, 0, noc_type 
        )
        phase.addTransfer(transfer)

    wl = npe.Workload()
    wl.addPhase(phase)

    npe_api = npe.API(npe.Config())
    result = npe_api.runNPE(wl)
    assert type(result) == npe.Stats

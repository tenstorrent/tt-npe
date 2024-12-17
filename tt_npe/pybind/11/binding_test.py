import time 
import tt_npe_pybind as npe

def main():

    c = npe.Config()
    c.congestion_model_name = "fast"
    c.set_verbosity_level(2)

    transfer = npe.createTransfer(
        2048, 10, npe.Coord(1, 1), npe.Coord(1, 5), 0.0, 0, npe.NocType.NOC_0
    )

    phase = npe.Phase()
    phase.addTransfer(transfer)

    wl = npe.Workload()
    wl.addPhase(phase)

    npe_api = npe.API(c)
    result = npe_api.runNPE(wl)

    if type(result) == npe.Stats:
        print(result)
    else:
        print("FAILED ")

main()
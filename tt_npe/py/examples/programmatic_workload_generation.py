#!/usr/bin/env python3

import argparse
import sys
import os
import random

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "lib"))

import tt_npe_pybind as npe

def log_error(msg):
    red  = "\u001b[31m"
    bold = "\u001b[1m"
    reset = "\u001b[0m"
    sys.stderr.write(f"{red}{bold}{msg}{reset}\n")

def genWorkload():
    wl = npe.Workload()
    phase = npe.Phase()

    # generate a randomized workload of 64 packets
    # all packets start from physical rows
    # noc type is randomized
    for i in range(30):
        src_row = random.randint(0, 8)
        dst_row = random.randint(0, 8)
        src_col = random.randint(1, 2)
        dst_col = random.randint(5, 6)

        # NOTE: npe.Coord is a pair of {row,col}, NOT {x,y}!
        # src locations are always where data is flowing FROM:
        #   For a WRITE, the Tensix initiating the noc call is the SRC
        #   For a READ, the Tensix initiating the noc call is the DST
        src_loc = npe.Coord(src_row, src_col)
        dst_loc = npe.Coord(dst_row, dst_col)

        # noc type specifies which NoC is used for this packet
        noctype = random.choice([npe.NocType.NOC_0, npe.NocType.NOC_1])

        # start offset controls the __earliest possible cycle__ this transfer
        # can start transfers may start later than this cycle, depending on VC
        # blocking effects and congestion. 
        start_offset = random.randint(0, 512)

        # injection rate (rate at which bytes are injected into the NIU at the
        # source location) is normally auto-inferred based on src location. If
        # desired, injection rate can be manually set if
        # `cfg.infer_injection_rate_from_src` is set to False.
        injection_rate = 0

        # packet size can be any number of bytes. However modelling will be more
        # accurate if larger contiguous transfers are broken down into smaller
        # series of packets that are max sized (20K transfer becomes three
        # packets i.e. [8k,8k,4k], since 8K is the max packet size on
        # wormhole_b0).
        packet_size = random.choice([1024, 2048, 4096, 8192])

        # num_packets must be at least 1; >1 packets are transferred
        # back-to-back in series; on some architectures this introduces
        # some overhead.
        num_packets = 1

        phase.addTransfer(
            npe.createTransfer(
                packet_size,
                num_packets,
                src_loc,
                dst_loc,
                injection_rate,
                start_offset,
                noctype,
            )
        )

    # add the populated phase to the workload
    wl.addPhase(phase)
    return wl

def main():

    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.set_verbosity_level(1)

    print("Creating workload programmatically ... ");
    wl = genWorkload()

    print("Starting tt-npe ... ");
    npe_api = npe.InitAPI(cfg)
    if npe_api is None:
        log_error(f"E: tt-npe could not be initialized, check that config is sound?")
        sys.exit(1)

    # run workload simulation using npe_api handle
    result = npe_api.runNPE(wl)
    match type(result):
        case npe.Stats:
            print(f"tt-npe simulation finished successfully in {result.wallclock_runtime_us} us!");
            print("--- stats ----------------------------------")
            print(result)
        case npe.Exception:
            log_error(f"E: tt-npe crashed during perf estimation: {result}")

if __name__ == "__main__":
    main()

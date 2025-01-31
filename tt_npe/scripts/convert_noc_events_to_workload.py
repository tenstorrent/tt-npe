#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import orjson
from typing import Any, Union, Dict, List
from pathlib import Path
import sys
import re
import argparse
from pprint import pprint

SUPPORTED_NOC_EVENTS = [ 
    "READ",
    "READ_SET_STATE",
    "READ_WITH_STATE",
    "READ_WITH_STATE_AND_TRID",
    "READ_DRAM_SHARDED_SET_STATE",
    "READ_DRAM_SHARDED_WITH_STATE",
    "WRITE_",
    "WRITE_MULTICAST",
    "WRITE_SET_STATE",
    "WRITE_WITH_STATE",
]

def load_json_file(file_path: Union[str, Path]) -> Union[Dict, List]:
    try:
        # Convert string path to Path object if necessary
        path = Path(file_path)
        
        # Check if file exists
        if not path.exists():
            raise FileNotFoundError(f"The file {path} does not exist")
            
        # Check if it's actually a file (not a directory)
        if not path.is_file():
            raise IsADirectoryError(f"{path} is a directory, not a file")
            
        # Read and parse the JSON file
        with path.open('r', encoding='utf-8') as file:
            # load file contents into str
            data = orjson.loads(file.read())
        return data
        
    except orjson.JSONDecodeError as e:
        raise e
    except PermissionError:
        raise PermissionError(f"Permission denied accessing {path}")
    except UnicodeDecodeError:
        raise UnicodeDecodeError(f"File {path} is not encoded in UTF-8")

def convert_noc_traces_to_npe_workload(input_filepath, output_filepath, quiet):

    def log(*args):
        if not quiet: 
            print(*args) 

    event_data_json = load_json_file(input_filepath)

    t0_timestamp = 2e30
    per_core_ts = {}
    for event in event_data_json:
        # find smallest timestamp
        ts = event.get("timestamp")
        if ts is not None:
            t0_timestamp = min(t0_timestamp,int(ts)) 
        else:
            continue

        # track max/min timestamp for each kernel
        proc = event.get("proc")
        sx = event.get("sx")
        sy = event.get("sy")
        if proc is not None and sx is not None and sy is not None:
            key = (proc,sx,sy)
            if not key in per_core_ts:
                per_core_ts[key] = {"max": 0, "min": 2**64}
            per_core_ts[key]["max"] = max(per_core_ts[key]["max"],ts)
            per_core_ts[key]["min"] = min(per_core_ts[key]["min"],ts)

    # figure out max kernel runtime
    max_kernel_cycles = 0
    for (proc,x,y),min_max_ts in per_core_ts.items():
        max_ts = min_max_ts["max"]
        min_ts = min_max_ts["min"]
        delta = max_ts - min_ts
        if delta > max_kernel_cycles:
            max_kernel_cycles = delta
            # print(f"{proc},{x},{y} is new max at {max_kernel_cycles} cycles")
    log(f"Longest running kernel took {max_kernel_cycles} cycles")

    # setup workload dict
    workload = {
        "golden_result": {"cycles": max_kernel_cycles},
        "phases": [],
    }

    log(f"converting all events")
    transfers = []
    idx = 0
    read_saved_state_sx = None
    read_saved_state_sy = None
    read_saved_state_dx = None
    read_saved_state_dy = None
    read_saved_state_num_bytes = None
    write_saved_state_sx = None
    write_saved_state_sy = None
    write_saved_state_dx = None
    write_saved_state_dy = None
    write_saved_state_num_bytes = None
    for event in event_data_json:
        proc = event.get("proc")
        noc_event_type = event.get("type")
        num_bytes = event.get("num_bytes",0)
        sx = event.get("sx")
        sy = event.get("sy")
        dx = event.get("dx")
        dy = event.get("dy")

        if noc_event_type not in SUPPORTED_NOC_EVENTS:
            continue

        if (noc_event_type is None) or (proc is None):
            continue 

        if (noc_event_type in ["WRITE_", "READ"]) and num_bytes == 0:
            log("WARNING: skipping event with 0 bytes!")
            continue

        # handle SET_STATE/WITH_STATE events
        if re.search("READ_(DRAM_SHARDED_)?SET_STATE",noc_event_type):
            read_saved_state_sx = sx
            read_saved_state_sy = sy
            read_saved_state_dx = dx
            read_saved_state_dy = dy
            read_saved_state_num_bytes = num_bytes
        elif re.search("READ_(DRAM_SHARDED_)?WITH_STATE(_AND_TRID)?",noc_event_type):
            sx = read_saved_state_sx
            sy = read_saved_state_sy
            dx = read_saved_state_dx
            dy = read_saved_state_dy
            num_bytes = read_saved_state_num_bytes
            #print("restoring saved state : ",dx,dy,num_bytes)
        elif re.search("WRITE_(DRAM_SHARDED_)?SET_STATE",noc_event_type):
            write_saved_state_sx = sx
            write_saved_state_sy = sy
            write_saved_state_dx = dx
            write_saved_state_dy = dy
            write_saved_state_num_bytes = num_bytes
        elif re.search("WRITE_(DRAM_SHARDED_)?WITH_STATE",noc_event_type):
            sx = write_saved_state_sx
            sy = write_saved_state_sy
            dx = write_saved_state_dx
            dy = write_saved_state_dy
            num_bytes = write_saved_state_num_bytes

        # invert src/dst convention here; READ data travels from remote L1 to the local L1
        if noc_event_type.startswith("READ"):
            sx,dx = dx,sx
            sy,dy = dy,sy

        try:
            ts = event.get("timestamp")
            phase_cycle_offset = int(ts) - t0_timestamp 
        except Exception as e:
            log(f"skipping conversion; timestamp could not be parsed '{ts}'")
            continue

        transfer = {}
        transfer["packet_size"] = num_bytes 
        transfer["num_packets"] = 1
        transfer["src_x"] = sx
        transfer["src_y"] = sy
        transfer["injection_rate"] = 0 # rely on IR inference within tt-npe
        transfer["phase_cycle_offset"] = phase_cycle_offset 
        transfer["noc_type"] = event.get("noc") 
        transfer["noc_event_type"] = noc_event_type if noc_event_type != "WRITE_" else "WRITE"

        if noc_event_type == "WRITE_MULTICAST":
            # NB: NOC_1 multicast has start_coord > end_coord; invert this so
            # tt-npe can use consistent convention
            if transfer["noc_type"] == "NOC_0":
                transfer["mcast_start_x"] = event.get("mcast_start_x")
                transfer["mcast_start_y"] = event.get("mcast_start_y")
                transfer["mcast_end_x"] = event.get("mcast_end_x")
                transfer["mcast_end_y"] = event.get("mcast_end_y")
            elif transfer["noc_type"] == "NOC_1":
                transfer["mcast_start_x"] = event.get("mcast_end_x")
                transfer["mcast_start_y"] = event.get("mcast_end_y")
                transfer["mcast_end_x"] = event.get("mcast_start_x")
                transfer["mcast_end_y"] = event.get("mcast_start_y")
        else:
            transfer["dst_x"] = dx
            transfer["dst_y"] = dy

        transfers.append(transfer) 

    phase = { "transfers" : transfers }
    workload["phases"].append(phase)

    log(f"Total transfers exported : {len(transfers)}")

    # Write workload to JSON file
    log(f"writing output workload to : {output_filepath}")
    with open(output_filepath, 'wb') as f:
        f.write(orjson.dumps(workload, option=orjson.OPT_INDENT_2))

    return len(transfers)


def get_cli_args():
    parser = argparse.ArgumentParser(
        description='Converts a noc event JSON trace into a tt-npe workload JSON file.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    parser.add_argument(
        'input_filepath',
        type=str,
        help='Path to the JSON file to load'
    )
    parser.add_argument(
        'output_filepath',
        type=str,
        help='Path to the JSON file to load'
    )
    parser.add_argument(
        '--quiet',
        action='store_true',
        help='Silence stdout'
    )
    return parser.parse_args()

def main():
    args = get_cli_args()
    convert_noc_traces_to_npe_workload(args.input_filepath, args.output_filepath, args.quiet)


if __name__ == "__main__":
    main()

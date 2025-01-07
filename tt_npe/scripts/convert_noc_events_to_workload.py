#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import json
import yaml 
from typing import Any, Union, Dict, List
from pathlib import Path
import sys
import re
import argparse
from pprint import pprint

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
            data = json.load(file)
            
        return data
        
    except json.JSONDecodeError as e:
        raise json.JSONDecodeError(
            f"Invalid JSON format in {path}: {str(e)}", 
            e.doc, 
            e.pos
        )
    except PermissionError:
        raise PermissionError(f"Permission denied accessing {path}")
    except UnicodeDecodeError:
        raise UnicodeDecodeError(f"File {path} is not encoded in UTF-8")

def convert_noc_traces_to_npe_workload(event_data_json, output_filepath, coalesce_packets):

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
    print(f"Longest running kernel took {max_kernel_cycles} cycles")

    # setup workload dict
    workload = {
        "golden_result": {"cycles": max_kernel_cycles},
        "phases": {"p1": {"transfers": {}}},
    }

    transfers = workload["phases"]["p1"]["transfers"]
    idx = 0
    last_transfer = None 
    transfers_coalesced = 0
    for event in event_data_json:
        proc = event.get("proc")
        type = event.get("type")
        num_bytes = event.get("num_bytes",0)

        # for now, filter out everything but vanilla RW events
        if (proc is None) or (not type in ["WRITE_", "READ"]) or (num_bytes == 0):
            if type and re.search("(SET|WITH)_STATE",type):
                print("WARNING: SET_STATE and WITH_STATE events are unsupported and will be ignored. See Issue tt-npe/issues/6 for details")
                print("WARNING: Ignoring event : ",event)
            continue

        # invert src/dst convention here; READ data travels from remote L1 to the local L1
        if type.startswith("READ"):
            sx = event.get("dx")
            sy = event.get("dy")
            dx = event.get("sx")
            dy = event.get("sy")
        else :
            sx = event.get("sx")
            sy = event.get("sy")
            dx = event.get("dx")
            dy = event.get("dy")

        try:
            ts = event.get("timestamp")
            phase_cycle_offset = int(ts) - t0_timestamp 
        except Exception as e:
            print(f"skipping conversion; timestamp could not be parsed '{ts}'")
            continue

        transfer = {}
        transfer["packet_size"] = num_bytes 
        transfer["num_packets"] = 1
        transfer["src_x"] = sx
        transfer["src_y"] = sy
        transfer["dst_x"] = dx
        transfer["dst_y"] = dy
        transfer["injection_rate"] = 0 # rely on IR inference within tt-npe
        transfer["phase_cycle_offset"] = phase_cycle_offset 
        transfer["noc_type"] = event.get("noc") 

        # optionally coalesce identical runs of packets into single transfers
        if coalesce_packets \
            and last_transfer is not None \
            and last_transfer["src_x"] == transfer["src_x"] and last_transfer["src_y"] == transfer["src_y"] \
            and last_transfer["dst_x"] == transfer["dst_x"] and last_transfer["dst_y"] == transfer["dst_y"] \
            and last_transfer["noc_type"] == transfer["noc_type"] \
            and last_transfer["packet_size"] == transfer["packet_size"]:
            last_transfer["num_packets"] += 1
            transfers_coalesced += 1
        else:
            last_transfer = transfers[f"tr{idx}"] = transfer
            idx += 1

    print(f"Total transfers exported : {len(transfers)}")
    if coalesce_packets: 
        print(f"Total transfers coalesced : {transfers_coalesced}")

    # Write YAML file
    print(f"writing output yaml workload to : {output_filepath}")
    with open(output_filepath, 'w') as file:
        yaml.dump(workload, file, default_flow_style=False, sort_keys=False, allow_unicode=True)


def get_cli_args():
    parser = argparse.ArgumentParser(
        description='Converts a noc event JSON trace into a tt-npe workload YAML file.',
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
        '--coalesce_packets',
        action='store_true',
        help='Coalesce adjacent reads/write calls into single logical transfers'
    )
    return parser.parse_args()

def main():
    args = get_cli_args()
    json_data = load_json_file(args.input_filepath)
    convert_noc_traces_to_npe_workload(json_data, args.output_filepath, args.coalesce_packets)


if __name__ == "__main__":
    main()

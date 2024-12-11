#!/usr/bin/env python3

import json
import yaml 
from typing import Any, Union, Dict, List
from pathlib import Path
import sys
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

def convert_noc_traces_to_npe_workload(event_data_json, output_filepath):

    t0_timestamp = 2e30
    for event in event_data_json:
        # find smallest timestamp
        ts = event.get("timestamp")
        if ts is not None:
            t0_timestamp = min(t0_timestamp,int(ts)) 

    workload = {"phases" : {"p1" : {"transfers" : {}}}}
    transfers = workload["phases"]["p1"]["transfers"]
    idx = 0
    for event in event_data_json:
        proc = event.get("proc")
        type = event.get("type")
        num_bytes = event.get("num_bytes",0)

        # for now, filter out everything but vanilla RW events
        if (proc is None) or (not type in ["WRITE_", "READ"]) or (num_bytes == 0):
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

        transfer = transfers[f"tr{idx}"] = {}
        idx += 1

        L1_INJECTION_RATE = 28.1
        transfer["packet_size"] = num_bytes 
        transfer["num_packets"] = 1
        transfer["src_x"] = sx
        transfer["src_y"] = sy
        transfer["dst_x"] = dx
        transfer["dst_y"] = dy
        transfer["injection_rate"] = L1_INJECTION_RATE
        transfer["phase_cycle_offset"] = phase_cycle_offset 
        transfer["noc_type"] = event.get("noc") 

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
        'file_path',
        type=str,
        help='Path to the JSON file to load'
    )
    return parser.parse_args()

def main():
    try:
        args = get_cli_args()
        json_data = load_json_file(args.file_path)
        p = Path(args.file_path)
        output_filepath = p.with_suffix('.yaml')
        convert_noc_traces_to_npe_workload(json_data, output_filepath)
        
    except Exception as e:
        print(f"Error loading JSON file: {str(e)}")

if __name__ == "__main__":
    main()
#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import argparse
import tempfile
import os
import glob
import sys
import re
from convert_noc_events_to_workload import convert_noc_traces_to_npe_workload

import tt_npe_pybind as npe

def get_cli_args():
    parser = argparse.ArgumentParser(
        description='Analyzes all JSON noc traces in a directory using tt-npe',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    parser.add_argument(
        'noc_trace_dir',
        type=str,
        help='The directory containing the JSON files of the NoC trace events'
    )
    return parser.parse_args()

def runNPE(opname,yaml_workload_file):
    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.workload_yaml_filepath = yaml_workload_file 

    wl = npe.createWorkloadFromYAML(cfg.workload_yaml_filepath)
    if wl is None:
        print(f"E: Could not create tt-npe workload from file '{yaml_workload_file}'; aborting ... ")
        sys.exit(1)

    npe_api = npe.InitAPI(cfg)
    if npe_api is None:
        print(f"E: tt-npe could not be initialized, check that config is sound?")
        sys.exit(1)

    # run workload simulation using npe_api handle
    result = npe_api.runNPE(wl)
    match type(result):
        case npe.Stats:
            #print(f"Overall Average Link Utilization: {result.overall_avg_link_util:5.1f}%")
            #print(f"Overall Maximum Link Utilization: {result.overall_max_link_util:5.1f}%")
            #print(f"Overall Average NIU  Utilization: {result.overall_avg_niu_util:5.1f}%")
            #print(f"Overall Maximum NIU  Utilization: {result.overall_max_niu_util:5.1f}%")
            print(f"{opname+',':42} {result.overall_avg_link_util:14.1f}, {result.overall_max_link_util:14.1f}, {result.overall_avg_niu_util:14.1f}, {result.overall_max_niu_util:14.1f}")
        case npe.Exception:
            print(f"E: tt-npe crashed during perf estimation: {result}")

def main():
    args = get_cli_args()
    # find all json files in the directory
    # for each json file, call convert_noc_traces_to_npe_workload
    # with the input and output file paths

    # Check if the directory exists
    if not os.path.isdir(args.noc_trace_dir):
        raise FileNotFoundError(f"The directory {args.noc_trace_dir} does not exist")

    # Print header
    print(f"{'opname':42} {'AVG LINK UTIL':>14}, {'MAX LINK UTIL':>14}, {'AVG NIU UTIL':>14}, {'MAX NIU UTIL':>14}")

    noc_trace_files = glob.glob(os.path.join(args.noc_trace_dir, "*.json"))
    for noc_trace_file in noc_trace_files:
        # create temp file name for output yaml file
        tmp_workload_file = tempfile.mktemp(".yaml", "tmp", ".")
        try:
            convert_noc_traces_to_npe_workload(
                noc_trace_file, tmp_workload_file, coalesce_packets=False, quiet=True
            )
            opname = os.path.basename(noc_trace_file).split(".")[0]
            opname = re.sub("noc_trace_dev\d*_", "", opname)
            runNPE(opname,tmp_workload_file)
        except Exception as e:
            print(f"Error processing {noc_trace_file}: {e}")
        finally:
            os.remove(tmp_workload_file)


if __name__ == "__main__":
    main()

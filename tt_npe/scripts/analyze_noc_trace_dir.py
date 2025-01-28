#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import argparse
import tempfile
import os
import glob
import sys
import re
import multiprocessing as mp 
from pathlib import Path
from convert_noc_events_to_workload import convert_noc_traces_to_npe_workload

import tt_npe_pybind as npe

TT_NPE_TMPFILE_PREFIX = "tt-npe-"
TMP_DIR = "/tmp/" if os.path.exists("/dev/shm") else "/tmp/"

# track stats accross tt-npe runs
class Stats:
    def __init__(self):
        self.datapoints = []

    def addDatapoint(self, avg_link_util, error, cycles):
        self.datapoints.append((avg_link_util, error, cycles))

    def getCycles(self):
        return sum([x[2] for x in self.datapoints])

    def getAvgLinkUtil(self):
        return sum([x[0] for x in self.datapoints]) / len(self.datapoints)

    def getWeightedAvgLinkUtil(self):
        total_cycles = self.getCycles()
        return sum([x[0] * x[2] for x in self.datapoints]) / total_cycles

    def getAvgError(self):
        return sum([x[1] for x in self.datapoints]) / len(self.datapoints)


def convert_and_run_noc_trace(noc_trace_file, output_dir, stats):
    try:
        tmp_workload_file = tempfile.mktemp(".json", TT_NPE_TMPFILE_PREFIX, TMP_DIR)
        num_transfers_converted = convert_noc_traces_to_npe_workload(
            noc_trace_file, tmp_workload_file, quiet=True
        )
        if num_transfers_converted > 0:
            opname = os.path.basename(noc_trace_file).split(".")[0]
            opname = re.sub("noc_trace_dev\d*_", "", opname)

            result = run_npe(opname, tmp_workload_file, output_dir)
            match type(result):
                case npe.Stats:
                    stats.addDatapoint(
                        result.overall_avg_link_util,
                        result.cycle_prediction_error,
                        result.golden_cycles,
                    )

    except Exception as e:
        print(f"Error processing {noc_trace_file}: {e}")
    finally:
        os.remove(tmp_workload_file)

def get_cli_args():
    parser = argparse.ArgumentParser(
        description="Analyzes all JSON noc traces in a directory using tt-npe",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "noc_trace_dir",
        type=str,
        help="The directory containing the JSON files of the NoC trace events",
    )
    return parser.parse_args()


def run_npe(opname, workload_file, output_dir):
    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.workload_json_filepath = workload_file
    cfg.set_verbosity_level(0)
    cfg.emit_stats_as_json = True
    cfg.stats_json_filepath = os.path.join(output_dir, opname + ".json")

    wl = npe.createWorkloadFromJSON(cfg.workload_json_filepath)
    if wl is None:
        print(
            f"E: Could not create tt-npe workload from file '{workload_file}'; aborting ... "
        )
        sys.exit(1)

    npe_api = npe.InitAPI(cfg)
    if npe_api is None:
        print(f"E: tt-npe could not be initialized, check that config is sound?")
        sys.exit(1)

    # run workload simulation using npe_api handle
    result = npe_api.runNPE(wl)
    match type(result):
        case npe.Stats:
            error = 100.0 * (
                (result.estimated_cycles - result.golden_cycles) / result.golden_cycles
            )
            print(
                f"{opname+',':42} {result.overall_avg_link_util:14.1f}, {result.overall_max_link_util:14.1f}, {error:14.1f}, {result.golden_cycles:14}"
            )
        case npe.Exception:
            print(f"E: tt-npe crashed during perf estimation: {result}")

    return result


def main():
    args = get_cli_args()
    # find all json files in the directory
    # for each json file, call convert_noc_traces_to_npe_workload
    # with the input and output file paths

    # cleanup old tmp files with prefix TT_NPE_TMPFILE_PREFIX
    for f in glob.glob(os.path.join(TMP_DIR,f"{TT_NPE_TMPFILE_PREFIX}*")):
        try:
            os.remove(f)
        except:
            print(f"WARN: Could not remove old tmp file '{f}'")

    # Check if the directory exists
    if not os.path.isdir(args.noc_trace_dir):
        raise FileNotFoundError(f"The directory {args.noc_trace_dir} does not exist")

    output_dir = os.path.join(
        "stats", os.path.basename(os.path.normpath(args.noc_trace_dir))
    )
    Path(output_dir).mkdir(parents=True, exist_ok=True)

    # Print header
    print(
        f"{'opname':42} {'AVG LINK UTIL':>14}, {'MAX LINK UTIL':>14}, {'% Error':>14}, {'CYCLES':>14}"
    )

    noc_trace_files = glob.glob(os.path.join(args.noc_trace_dir, "*.json"))
    running_tasks = []
    # sort the files by their size, largest first
    noc_trace_files.sort(key=os.path.getsize, reverse=True)

    stats = Stats()
    for noc_trace_file in noc_trace_files:
        convert_and_run_noc_trace(noc_trace_file, output_dir, stats)

    # wait for async tasks to complete
    for task in running_tasks:
        task.get()

    print("-------")
    print(f"average cycle prediction error   : {stats.getAvgError():.2f} ")
    print(f"average link util                : {stats.getAvgLinkUtil():.2f} ")
    print(f"cycle-weighted overall link util : {stats.getWeightedAvgLinkUtil():.2f} ")

if __name__ == "__main__":
    main()

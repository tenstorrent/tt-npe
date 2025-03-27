#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

import argparse
import tempfile
import os
import glob
import sys
import re
import shutil
import random
import multiprocessing as mp 
from pathlib import Path
from npe_convert_noc_events_to_workload import convert_noc_traces_to_npe_workload

import tt_npe_pybind as npe
from multiprocessing import Pool
from functools import partial
    
BOLD = '\033[1m'
RESET = '\033[0m'
GREEN = '\033[32m'

TT_NPE_TMPFILE_PREFIX = "tt-npe-"
TMP_DIR = "/tmp/" 

def update_message(message, quiet):
    if quiet: return
    clear_line = ' ' * shutil.get_terminal_size().columns
    sys.stdout.write('\r' + clear_line + '\r' + message)
    sys.stdout.flush()

# track stats accross tt-npe runs
class Stats:
    class Datapoint:
        def __init__(self, op_name, op_id, result):
            self.op_name = op_name
            self.op_id = op_id
            self.result = result

    def __init__(self):
        self.datapoints = {}

    def __iter__(self):
        for k,v in self.datapoints.items():
            yield (k,v)

    def addDatapoint(self, op_name, op_id, result):
        self.datapoints[op_id] = Stats.Datapoint(op_name, op_id, result)

    def getDatapointByID(self, op_id):
        return self.datapoints.get(op_id,None)

    def getSortedEvents(self):
        return sorted(self.datapoints.values(), key=lambda dp: 1.0/dp.result.golden_cycles)

    def getCycles(self):
        return sum([dp.result.golden_cycles for dp in self.datapoints.values()])

    def getAvgLinkUtil(self):
        return sum([dp.result.overall_avg_link_util for dp in self.datapoints.values()]) / len(
            self.datapoints.values()
        )

    def getWeightedAvgLinkUtil(self):
        total_cycles = self.getCycles()
        return (
            sum(
                [
                    dp.result.overall_avg_link_util * dp.result.golden_cycles
                    for dp in self.datapoints.values()
                ]
            )
            / total_cycles
        )

    def getWeightedAvgDramBWUtil(self):
        total_cycles = self.getCycles()
        return (
            sum(
                [
                    dp.result.dram_bw_util * dp.result.golden_cycles
                    for dp in self.datapoints.values()
                ]
            )
            / total_cycles
        )

    def getAvgError(self):
        return sum([abs(dp.result.cycle_prediction_error) for dp in self.datapoints.values()]) / len(
            self.datapoints.values()
        )
    def getErrorPercentiles(self):
        errors = [abs(dp.result.cycle_prediction_error) for dp in self.datapoints.values()]
        errors.sort()
        return {
            "25th_percentile": errors[int(len(errors) * 0.25)],
            "50th_percentile": errors[len(errors) // 2],
            "75th_percentile": errors[int(len(errors) * 0.75)],
            "worst": errors[-1],
        }

def process_trace(noc_trace_info, output_dir, emit_stats_as_json):
    noc_trace_file, opname, op_id = noc_trace_info
    try:
        result = run_npe(opname, noc_trace_file, output_dir, emit_stats_as_json)
        if type(result) == npe.Stats:
            return (opname, op_id, result)
    except Exception as e:
        print(f"Error processing {noc_trace_file}: {e}")
    return None

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
    parser.add_argument(
        "-e","--emit_stats_as_json",
        action="store_true",
        help="Emit the stats as JSON files",
    )
    parser.add_argument(
        "-q","--quiet",
        action="store_true",
        help="Mute logging",
    )
    parser.add_argument(
        "-s","--show_accuracy_stats",
        action="store_true",
        help="Show accuracy stats",
    )
    return parser.parse_args()


def run_npe(opname, workload_file, output_dir, emit_stats_as_json):
    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.workload_json_filepath = workload_file
    #cfg.congestion_model_name = "fast"
    cfg.cycles_per_timestep = 32
    cfg.workload_is_noc_trace = True
    cfg.set_verbosity_level(0)
    if emit_stats_as_json:
        cfg.emit_stats_as_json = True
        cfg.stats_json_filepath = os.path.join(output_dir, opname + ".json")

    wl = npe.createWorkloadFromJSON(cfg.workload_json_filepath, is_noc_trace_format=True)
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
    if type(result) == npe.Exception:
        print(f"E: tt-npe crashed during perf estimation: {result}")

    return result

def print_stats_summary_table(stats, show_accuracy_stats=False):
    # Print header
    print("--------------------------------------------------------------------------------------------------------------------")
    print(
            f"{BOLD}{'Opname':42} {'Op ID':>5} {'NoC Util':>14} {'DRAM BW Util':>14} {'Cong Impact':>14} {'% Overall Cycles':>19}{RESET}"
    )
    print("--------------------------------------------------------------------------------------------------------------------")

    # print data for each operation's noc trace
    for dp in stats.getSortedEvents():
        pct_total_cycles = 100.0 * (dp.result.golden_cycles / stats.getCycles())
        print(
                f"{dp.op_name:42} {dp.op_id:>5} {dp.result.overall_avg_link_util:>13.1f}% {dp.result.dram_bw_util:13.1f}% {dp.result.getCongestionImpact():>13.1f}% {pct_total_cycles:>18.1f}% {dp.result.wallclock_runtime_us/1000:18.1f} ms"
        )

    print("--------------------------------------------------------------------------------------------------------------------")
    if show_accuracy_stats:
        print(f"average cycle prediction error   : {stats.getAvgError():.2f} ")
        print(f"error percentiles : ")
        for k, v in stats.getErrorPercentiles().items():
            print(f"  {k:15} : {v:4.1f}%")
    print(f"average link util                : {stats.getAvgLinkUtil():.1f}% ")
    print(f"cycle-weighted overall link util : {stats.getWeightedAvgLinkUtil():.1f}% ")
    print(f"cycle-weighted dram bw util      : {stats.getWeightedAvgDramBWUtil():.1f}% ")

def extractOpNameAndIDFromFilename(noc_trace_file):
    basename = os.path.basename(noc_trace_file)
    op_name = re.search("(noc_trace_dev\d+_)?(\w*?)(_ID\d*)?\.json", basename).group(2)
    op_id_match = re.search("_ID(\d+)", basename)
    if op_id_match:
        op_id = int(op_id_match.group(1))
    else:
        op_id = None
    return op_name, op_id

def analyze_noc_traces_in_dir(noc_trace_dir, emit_stats_as_json, quiet=False, show_accuracy_stats=False): 
    # cleanup old tmp files with prefix TT_NPE_TMPFILE_PREFIX
    for f in glob.glob(os.path.join(TMP_DIR,f"{TT_NPE_TMPFILE_PREFIX}*")):
        try:
            os.remove(f)
        except:
            print(f"WARN: Could not remove old tmp file '{f}'")

    # Check if the directory exists
    if not os.path.isdir(noc_trace_dir):
        raise FileNotFoundError(f"The directory {noc_trace_dir} does not exist")

    output_dir = os.path.join(
        os.path.dirname(os.path.normpath(noc_trace_dir)), "npe_stats"
    )
    if emit_stats_as_json:
        Path(output_dir).mkdir(parents=True, exist_ok=True)

    noc_trace_files = glob.glob(os.path.join(noc_trace_dir, "*.json"))
    if len(noc_trace_files) == 0:
        print(f"Error: No JSON trace files found in {noc_trace_dir}")
        sys.exit(1)
    noc_trace_files = sorted(noc_trace_files)

    # random non-overlapping set of op ids to assign to traces without an id
    random_op_ids = random.sample(range(100000,1000000), len(noc_trace_files))

    # extract opname and opid from filename
    noc_trace_info = []
    for noc_trace_file in noc_trace_files:
        op_name, op_id = extractOpNameAndIDFromFilename(noc_trace_file)
        if op_id is None:
            op_id = random_op_ids.pop()
        noc_trace_info.append((noc_trace_file, op_name, op_id))

    stats = Stats()
    with Pool(processes=mp.cpu_count()) as pool:
        process_func = partial(process_trace, output_dir=output_dir, emit_stats_as_json=emit_stats_as_json)
        for i, result in enumerate(pool.imap_unordered(process_func, noc_trace_info)):
            update_message(f"Analyzing ({i + 1}/{len(noc_trace_files)}) ...", quiet)
            if result:
                op_name, op_id, result_data = result
                stats.addDatapoint(op_name, op_id, result_data)
    update_message("\n", quiet)

    if not quiet:
        print_stats_summary_table(stats, show_accuracy_stats)
        if emit_stats_as_json:
            print(f"\n👉 {BOLD}{GREEN}ttnn-visualizer files located in: '{output_dir}'{RESET}")

    return stats

def main():
    args = get_cli_args()
    analyze_noc_traces_in_dir(args.noc_trace_dir, args.emit_stats_as_json, args.quiet, args.show_accuracy_stats)


if __name__ == "__main__":
    main()

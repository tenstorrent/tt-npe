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
import multiprocessing as mp 
from pathlib import Path
from convert_noc_events_to_workload import convert_noc_traces_to_npe_workload

import tt_npe_pybind as npe

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

def convert_and_run_noc_trace(noc_trace_file, output_dir, emit_stats_as_json):
    try:
        tmp_workload_file = tempfile.mktemp(".json", TT_NPE_TMPFILE_PREFIX, TMP_DIR)
        num_transfers_converted = convert_noc_traces_to_npe_workload(
            noc_trace_file, tmp_workload_file, quiet=True
        )
        opname = os.path.basename(noc_trace_file).split(".")[0]
        opname = re.sub("noc_trace_dev\d*_", "", opname)

        return run_npe(opname, tmp_workload_file, output_dir, emit_stats_as_json)

    except Exception as e:
        print(f"Error processing {noc_trace_file}: {e}")
    finally:
        try:
            os.remove(tmp_workload_file)
        except:
            pass

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
    return parser.parse_args()


def run_npe(opname, workload_file, output_dir, emit_stats_as_json):
    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.workload_json_filepath = workload_file
    #cfg.congestion_model_name = "fast"
    cfg.cycles_per_timestep = 32
    cfg.set_verbosity_level(0)
    if emit_stats_as_json:
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
    if type(result) == npe.Exception:
        print(f"E: tt-npe crashed during perf estimation: {result}")

    return result

def print_stats_summary_table(stats):
    # Print header
    BOLD = '\033[1m'
    RESET = '\033[0m'
    GREEN = '\033[32m'
    print("--------------------------------------------------------------------------------------------------------------------")
    print(
            f"{BOLD}{'Opname':42} {'Op ID':>5} {'NoC Util':>14} {'DRAM BW Util':>14} {'Cong Impact':>14} {'% Overall Cycles':>19}{RESET}"
    )
    print("--------------------------------------------------------------------------------------------------------------------")

    # print data for each operation's noc trace
    for dp in stats.getSortedEvents():
        pct_total_cycles = 100.0 * (dp.result.golden_cycles / stats.getCycles())
        print(
                f"{dp.op_name:42} {dp.op_id:>5} {dp.result.overall_avg_link_util:>13.1f}% {dp.result.dram_bw_util:13.1f}% {dp.result.getCongestionImpact():>13.1f}% {pct_total_cycles:>18.1f}%"
        )

    print("--------------------------------------------------------------------------------------------------------------------")
    #print(f"average cycle prediction error   : {stats.getAvgError():.2f} ")
    #print(f"error percentiles : ")
    #for k, v in stats.getErrorPercentiles().items():
    #    print(f"  {k:15} : {v:4.1f}%")
    print(f"average link util                : {stats.getAvgLinkUtil():.1f}% ")
    print(f"cycle-weighted overall link util : {stats.getWeightedAvgLinkUtil():.1f}% ")
    print(f"cycle-weighted dram bw util      : {stats.getWeightedAvgDramBWUtil():.1f}% ")


def analyze_noc_traces_in_dir(noc_trace_dir, emit_stats_as_json, quiet=False): 
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

    # sort the files by their size, largest first
    #noc_trace_files.sort(key=os.path.getsize, reverse=True)

    stats = Stats()
    for i,noc_trace_file in enumerate(noc_trace_files):
        update_message(f"Analyzing ({i}/{len(noc_trace_files)}) {noc_trace_file} ...", quiet)
        result = convert_and_run_noc_trace(noc_trace_file, output_dir, emit_stats_as_json)
        if type(result) == npe.Exception:
                print(f"E: tt-npe crashed during perf estimation: {result}")
                continue
        elif type(result) == npe.Stats:
           basename = os.path.basename(noc_trace_file)
           basename = re.sub("noc_trace_dev\d*_", "", basename)    
           op_name = re.search("(\w*)(_ID)?",basename).group(1)
           op_id_match = re.search("_ID(\d+)", basename)
           op_id = op_id_match.group(1) if op_id_match else -1
           stats.addDatapoint(op_name, int(op_id), result)
    update_message("\n", quiet)

    if not quiet:
        print_stats_summary_table(stats)
        if emit_stats_as_json:
            print(f"\n👉 {BOLD}{GREEN}ttnn-visualizer files located in: '{output_dir}'{RESET}")

    return stats

def main():
    args = get_cli_args()
    analyze_noc_traces_in_dir(args.noc_trace_dir, args.emit_stats_as_json, args.quiet)


if __name__ == "__main__":
    main()

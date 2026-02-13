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
import orjson
from pathlib import Path

import tt_npe_pybind as npe
from multiprocessing import Pool
from functools import partial
from fabric_post_process import TopologyGraph, process_traces, log_info, log_warning, log_error
    
BOLD = '\033[1m'
RESET = '\033[0m'
GREEN = '\033[32m'

TT_NPE_TMPFILE_PREFIX = "tt-npe-"
TMP_DIR = "/tmp/" 

def erase_previous_line():
    print('\033[1A', end='')  # Move cursor up one line
    print('\033[2K', end='')  # Clear the entire line
    print('\r', end='')       # Move cursor to beginning of line

def update_message(message, quiet):
    if quiet: return
    erase_previous_line()
    sys.stdout.write(message + '\n')
    sys.stdout.flush()

# track stats accross tt-npe runs
class Stats:
    class Datapoint:
        def __init__(self, op_name, op_uid, device_id, result):
            self.op_name = op_name
            self.op_uid = op_uid
            self.device_id = device_id
            self.result = result

    def __init__(self):
        self.datapoints = {}

    def __iter__(self):
        for k,v in self.datapoints.items():
            yield (k,v)

    def addDatapoint(self, op_name, op_uid, device_id, result):
        self.datapoints[(op_uid, device_id)] = Stats.Datapoint(op_name, op_uid, device_id, result)

    def getDatapointByID(self, program_runtime_id, metal_trace_id, metal_trace_replay_session_id):
        combined_trace_id = metal_trace_id << 32 | metal_trace_replay_session_id if metal_trace_id is not None else None
        return self.datapoints.get(
            (OpUID(get_ttnn_op_id(program_runtime_id), combined_trace_id), get_device_id(program_runtime_id)), None)

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

    def getWeightedAvgEthBWUtil(self):
        total_cycles = self.getCycles()
        return (
            sum(
                [
                    dp.result.getAggregateEthBwUtil() * dp.result.golden_cycles
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

class OpUID:
    def __init__(self, ttnn_op_id, metal_trace_id):
        self.ttnn_op_id = ttnn_op_id
        # this field combines both the trace id and trace id counter (or replay session id) in metal traced runs
        self.metal_trace_id = metal_trace_id
    def __str__(self):
        return f"ID{self.ttnn_op_id}" + (f"_traceID{self.metal_trace_id}" if self.metal_trace_id else "")
    def __hash__(self):
        return hash((self.ttnn_op_id, self.metal_trace_id))
    def __eq__(self, other):
        if not isinstance(other, OpUID):
            return False
        return self.ttnn_op_id == other.ttnn_op_id and self.metal_trace_id == other.metal_trace_id

def process_trace(noc_trace_info, device_name, topology_json_file, compress_timeline_files, output_dir, emit_viz_timeline_files, timeline_split_threshold):
    noc_trace_file, opname, op_uid = noc_trace_info
    try:
        result = run_npe(opname, op_uid, device_name, noc_trace_file, topology_json_file, compress_timeline_files, output_dir, emit_viz_timeline_files, timeline_split_threshold)
        if isinstance(result, npe.Stats):
            return (opname, op_uid, result)
        else:
            log_error(f"tt-npe exited unsuccessfully with error {result} on trace {os.path.basename(noc_trace_file)}\n")
    except Exception as e:
        log_error(f"Error processing {noc_trace_file}: {e}\n")
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
        "-e","--emit_viz_timeline_files",
        action="store_true",
        help="Emit visualization timeline files",
    )
    parser.add_argument(
        "--compress_timeline_files",
        default=False,
        action="store_true",
        help="Emit visualization timeline files",
    )
    parser.add_argument(
        "--group_as_metal_traces",
        default=False,
        action="store_true",
        help="Use if tracing a metal program (where there is no mesh workload id for grouping traces)",
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
    parser.add_argument(
        "-m", "--max_rows_in_summary_table",
        type=int,
        default=40,
        help="Maximum number of rows to display in summary table",
    )
    parser.add_argument(
        "--timeline_split_threshold",
        type=int,
        default=10000,
        help="Threshold (in timesteps) for splitting timeline files",
    )
    return parser.parse_args()


def run_npe(opname, op_uid, device_name, workload_file, topology_json_file, compress_timeline_files, output_dir, emit_viz_timeline_files, timeline_split_threshold):
    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.device_name = device_name
    cfg.workload_json_filepath = workload_file
    #cfg.congestion_model_name = "fast"
    cfg.cycles_per_timestep = 32
    cfg.workload_is_noc_trace = True
    cfg.set_verbosity_level(0)
    if emit_viz_timeline_files:
        cfg.emit_timeline_file = True
        cfg.timeline_filepath = os.path.join(output_dir, f"{opname}_{str(op_uid)}.npeviz")
    cfg.compress_timeline_output_file = compress_timeline_files
    cfg.topology_json = topology_json_file
    cfg.timeline_split_threshold_timesteps = timeline_split_threshold

    wl = npe.createWorkloadFromJSON(cfg.workload_json_filepath, cfg.device_name, is_noc_trace_format=True)
    if wl is None:
        raise Exception(f"Could not create tt-npe workload from file '{workload_file}'; aborting ... ")

    npe_api = npe.InitAPI(cfg)
    if npe_api is None:
        raise Exception(f"tt-npe could not be initialized, check that config is sound?")

    # run workload simulation using npe_api handle
    return npe_api.runNPE(wl)

def print_stats_summary_table(stats, show_accuracy_stats=False, max_display=40):
    # Print header
    print("---------------------------------------------------------------------------------------------------------------------------------------------------------")
    print(
            f"{BOLD}{'Opname':42} {'Op ID':>5} {'Metal Trace ID':>15} {'NoC Util':>14} {'DRAM BW Util':>14} {'Cong Impact':>14} {'% Overall Cycles':>19} {'ETH BW Util (per core)'}{RESET}"
    )
    print("---------------------------------------------------------------------------------------------------------------------------------------------------------")

    # print data for each operation's noc trace
    sorted_events = stats.getSortedEvents()
    for i, dp in enumerate(sorted_events):
        if i >= max_display:
            remaining = len(sorted_events) - max_display
            print(f"... {remaining} operations omitted; use -m $NUM_ROWS to display more")
            break
        pct_total_cycles = 100.0 * (dp.result.golden_cycles / stats.getCycles())
        metal_trace_id_str = "" if dp.op_uid.metal_trace_id is None else str(dp.op_uid.metal_trace_id)
        print(
                f"{dp.op_name:42} {dp.op_uid.ttnn_op_id:>5} {metal_trace_id_str:>15} {dp.result.overall_avg_link_util:>13.2f}% {dp.result.dram_bw_util:13.2f}% {dp.result.getCongestionImpact():>13.2f}% {pct_total_cycles:>18.2f}%  {dp.result.getEthBwUtilPerCoreStr()}"
        )

    print("---------------------------------------------------------------------------------------------------------------------------------------------------------")
    if show_accuracy_stats:
        print(f"average cycle prediction error   : {stats.getAvgError():.2f} ")
        print(f"error percentiles : ")
        for k, v in stats.getErrorPercentiles().items():
            print(f"  {k:15} : {v:4.1f}%")
    print(f"average link util                : {stats.getAvgLinkUtil():.1f}% ")
    print(f"cycle-weighted overall link util : {stats.getWeightedAvgLinkUtil():.1f}% ")
    print(f"cycle-weighted dram bw util      : {stats.getWeightedAvgDramBWUtil():.1f}% ")
    print(f"cycle-weighted eth bw util       : {stats.getWeightedAvgEthBWUtil():.1f}% ")

def extractDataFromFilename(noc_trace_file):
    basename = os.path.basename(noc_trace_file)
    match = re.search("^noc_trace_dev(\d+)_((\w*)_)?ID(\d+)(_traceID(\d+))?\.json$", basename)
    if match:
        dev_id = int(match.group(1))
        program_runtime_id = int(match.group(4))
        op_name = match.group(3) if match.group(3) is not None else "UnknownOP"
        metal_trace_id = int(match.group(6)) if match.group(6) is not None else None
        return dev_id, op_name, program_runtime_id, metal_trace_id
    else:
        return None, None, None, None

# extract dev id, opname, and opid from filename and group by aligned op ids per device
def group_traces_metal(noc_trace_files):
    # group traces per device
    noc_trace_files_per_device = {}
    for noc_trace_file in noc_trace_files:
        dev_id, op_name, program_runtime_id, metal_trace_id = extractDataFromFilename(noc_trace_file)
        if program_runtime_id is None:
            print(f"Invalid name for noc trace file: {noc_trace_file}. Skipping...")
            continue

        assert(op_name == "UnknownOP")
        assert(metal_trace_id == None)

        if dev_id not in noc_trace_files_per_device:
            noc_trace_files_per_device[dev_id] = []
        
        noc_trace_files_per_device[dev_id].append((program_runtime_id, noc_trace_file))

    # sort by op id for each device
    num_ops = 0
    devices = []
    for dev_id, trace_files in noc_trace_files_per_device.items():
        trace_files.sort(key=lambda trace_file_and_info: trace_file_and_info[0])
        num_ops = max(num_ops, len(trace_files))
        devices.append(dev_id)

    noc_trace_files_per_op = []
    for i in range(num_ops):
        op_trace_files = []
        max_program_runtime_id = -1
        for dev_id in devices:
            if (i >= len(noc_trace_files_per_device[dev_id])):
                log_error(f"E: Trace file missing for op on device {dev_id}")
                continue

            program_runtime_id, noc_trace_file = noc_trace_files_per_device[dev_id][i]
            op_trace_files.append(noc_trace_file)
            # use max_program_runtime_id as an op_id
            max_program_runtime_id = max(max_program_runtime_id, program_runtime_id)

        op_uid = OpUID(max_program_runtime_id, metal_trace_id)
        noc_trace_files_per_op[op_uid] = ("UnknownOP", op_trace_files)

    return noc_trace_files_per_op

def get_device_id(program_runtime_id):
    # program_runtime_id: ttnn_op_id (21 bits) | device id (10 bits)
    return program_runtime_id & 0x3FF  # mask lower 10 bits

def get_ttnn_op_id(program_runtime_id):
    # program_runtime_id: ttnn_op_id (21 bits) | device id (10 bits)
    return program_runtime_id >> 10

def get_program_runtime_id(ttnn_op_id, device_id):
    # program_runtime_id: ttnn_op_id (21 bits) | device id (10 bits)
    return ttnn_op_id << 10 | device_id

def group_traces_ttnn(noc_trace_files):
    # extract dev id, opname, ttnn op id, and trace replay id from filename and group
    # by ttnn op id, and trace replay
    noc_trace_files_per_op = {}
    devices_per_op = {} # to check for mutiple trace runs
    for noc_trace_file in noc_trace_files:
        dev_id, op_name, program_runtime_id, metal_trace_id = extractDataFromFilename(noc_trace_file)
        if program_runtime_id is None:
            print(f"Invalid name for noc trace file: {noc_trace_file}. Skipping...")
            continue

        op_uid = OpUID(get_ttnn_op_id(program_runtime_id), metal_trace_id)

        if op_uid not in devices_per_op:
            devices_per_op[op_uid] = []
        # There cannot be multiple traces for a device in a single op, this likely means there are old traces present too
        if (dev_id in devices_per_op[op_uid]):
            log_error("There seem to trace files from multiple runs in the trace directory, please remove old traces and run again.")
        devices_per_op[op_uid].append(dev_id)

        if op_uid not in noc_trace_files_per_op:
            noc_trace_files_per_op[op_uid] = (op_name, [])
        
        # check op name matches
        if noc_trace_files_per_op[op_uid][0] != op_name:
            log_error("Op names do not match for the same op id. There seem to trace files from multiple runs in the trace directory, please remove old traces and run again.")

        noc_trace_files_per_op[op_uid][1].append(noc_trace_file)

    return noc_trace_files_per_op

def analyze_noc_traces_in_dir(noc_trace_dir, emit_viz_timeline_files, compress_timeline_files=False, group_as_metal_traces = False,
        quiet=False, show_accuracy_stats=False, max_rows_in_summary_table=40, timeline_split_threshold=10000): 
    # cleanup old tmp files with prefix TT_NPE_TMPFILE_PREFIX
    for f in glob.glob(os.path.join(TMP_DIR,f"{TT_NPE_TMPFILE_PREFIX}*")):
        try:
            os.remove(f)
        except:
            print(f"WARN: Could not remove old tmp file '{f}'")

    # Check if the directory exists
    if not os.path.isdir(noc_trace_dir):
        raise FileNotFoundError(f"The directory {noc_trace_dir} does not exist")

    # check if device sync was used
    if not os.path.isfile(os.path.join(noc_trace_dir, "sync_device_info.csv")):
        log_warning("Device sync isn't turned on. This will lead to inaccurate traces for mesh device workloads! " + 
        "If you are tracing a workload on a mesh device, please turn on device sync by setting " +
        "TT_METAL_DEVICE_PROFILER=1 and TT_METAL_PROFILER_SYNC=1 then re-collect traces")

    output_dir = os.path.join(
        os.path.dirname(os.path.normpath(noc_trace_dir)), "npe_viz"
    )
    if emit_viz_timeline_files:
        Path(output_dir).mkdir(parents=True, exist_ok=True)

    noc_trace_files = glob.glob(os.path.join(noc_trace_dir, "noc_trace*.json"))
    # filter out already merged trace files
    noc_trace_files = list(filter(lambda tracename: "merged.json" not in tracename, noc_trace_files))
    if len(noc_trace_files) == 0:
        print(f"Error: No JSON trace files found in {noc_trace_dir}")
        sys.exit(1)

    # group traces
    if (group_as_metal_traces):
        noc_trace_files_per_op = group_traces_metal(noc_trace_files)
    else:
        noc_trace_files_per_op = group_traces_ttnn(noc_trace_files)

    # run fabric post process to merge grouped traces 
    topology_file_path = os.path.join(noc_trace_dir, "topology.json")
    topology = TopologyGraph(topology_file_path)
    device_name = topology.cluster_type
    noc_trace_info = []
    remove_desynchronized_events = True
    for op_uid, op_name_and_trace_files in noc_trace_files_per_op.items():
        op_name, op_trace_files = op_name_and_trace_files
        output_file_path = os.path.join(noc_trace_dir, f"noc_trace_{str(op_uid)}_merged.json")
        process_traces(
            topology, op_trace_files, output_file_path, remove_desynchronized_events, quiet
        )
        noc_trace_info.append((output_file_path, op_name, op_uid))

    log_info("Trace merging complete, running NPE next.", quiet)
    log_info("Analyzing traces...", quiet)
    stats = Stats()
    timeline_files = []
    with Pool(processes=mp.cpu_count()) as pool:
        process_func = partial(process_trace, device_name=device_name, topology_json_file=topology_file_path, 
        compress_timeline_files=compress_timeline_files, output_dir=output_dir, emit_viz_timeline_files=emit_viz_timeline_files,
        timeline_split_threshold=timeline_split_threshold)
        for i, result in enumerate(pool.imap_unordered(process_func, noc_trace_info)):
            update_message(f"Analyzing ({i + 1}/{len(noc_trace_info)}) ...", quiet)
            if result is not None:
                op_name, op_uid, result_data = result
                for device_id, device_stats in result_data.per_device_stats.items():
                    if device_id == -1:
                        continue
                    stats.addDatapoint(op_name, op_uid, device_id, device_stats)
                program_runtime_id = op_uid.ttnn_op_id if group_as_metal_traces else get_program_runtime_id(op_uid.ttnn_op_id, 0)
                timeline_files.append({"global_call_count": program_runtime_id, "file": f"{op_name}_{str(op_uid)}.npeviz" 
                    + (".zst" if compress_timeline_files else "")})
    update_message("\n", quiet)

    # create manifest file
    if emit_viz_timeline_files:
        manifest_file_path = os.path.join(output_dir, "manifest.json")
        with open(manifest_file_path, "wb") as f:
            f.write(orjson.dumps(timeline_files, option=orjson.OPT_INDENT_2))
    
    if not quiet:
        print_stats_summary_table(stats, show_accuracy_stats, max_rows_in_summary_table)
        if emit_viz_timeline_files:
            print(f"\n👉 {BOLD}{GREEN}ttnn-visualizer files located in: '{output_dir}'{RESET}")

    return stats

def main():
    args = get_cli_args()
    analyze_noc_traces_in_dir(args.noc_trace_dir, args.emit_viz_timeline_files, args.compress_timeline_files, args.group_as_metal_traces, args.quiet, args.show_accuracy_stats, args.max_rows_in_summary_table, args.timeline_split_threshold)


if __name__ == "__main__":
    main()

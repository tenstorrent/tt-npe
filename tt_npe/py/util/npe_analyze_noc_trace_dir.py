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
import copy
import csv

import tt_npe_pybind as npe
from multiprocessing import Pool
from functools import partial
from fabric_post_process import TopologyGraph, merge_traces, is_ccl_op, log_info, log_warning, log_error, log_debug
    
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
        return self.datapoints.get(get_ttnn_op_id(op_id),None)

    def getSortedEvents(self):
        return self.datapoints.values() #sorted(self.datapoints.values(), key=lambda dp: 1.0/dp.result.golden_cycles)

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

def process_trace(noc_trace_file, device_name, topology_json_file, single_device_op, emit_viz_timeline_files, timeline_filepath, compress_timeline_files):
    try:
        result = run_npe(device_name, noc_trace_file, topology_json_file, single_device_op, emit_viz_timeline_files, timeline_filepath, compress_timeline_files)
        #if isinstance(result, npe.Stats):
        return result
        #else:
        #    log_error(f"E: tt-npe exited unsuccessfully with error {result} on trace {os.path.basename(noc_trace_file)}\n")
    except Exception as e:
        log_error(f"E: Error processing {noc_trace_file}: {e}\n")
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
    return parser.parse_args()


def run_npe(device_name, workload_file, topology_json_file, single_device_op, emit_viz_timeline_files, timeline_filepath, compress_timeline_files):
    # populate Config struct from cli args
    cfg = npe.Config()
    cfg.device_name = device_name
    cfg.workload_json_filepath = workload_file
    #cfg.congestion_model_name = "fast"
    cfg.cycles_per_timestep = 32
    cfg.workload_is_noc_trace = True
    cfg.single_device_op = single_device_op
    cfg.set_verbosity_level(0)
    if emit_viz_timeline_files:
        cfg.emit_timeline_file = True
        cfg.timeline_filepath = timeline_filepath
    cfg.compress_timeline_output_file = compress_timeline_files
    cfg.topology_json = topology_json_file

    wl = npe.createWorkloadFromJSON(cfg.workload_json_filepath, cfg.device_name, is_noc_trace_format=True)
    if wl is None:
        log_error(f"E: Could not create tt-npe workload from file '{workload_file}'; aborting ... ")
        sys.exit(1)

    npe_api = npe.InitAPI(cfg)
    if npe_api is None:
        print(f"E: tt-npe could not be initialized, check that config is sound?")
        sys.exit(1)

    # run workload simulation using npe_api handle
    return npe_api.runNPE(wl)

def write_csv_output(stats, csv_output_path, noc_trace_files_per_op):
    """Write operation statistics to a CSV file."""
    sorted_events = stats.getSortedEvents()
    with open(csv_output_path, 'w', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(['Opname', 'Op_ID', 'TTNN_OP_ID', 'Device_ID', 'TYPE', 'NoC_Util_Percent', 'DRAM_BW_Util_Percent', 'Cong_Impact_Percent', 'Percent_Overall_Cycles', 'Kernel_Duration', 'Subdevice_Events'])
        
        for dp in sorted_events:            
            # Get subdevice events count and operation type
            subdevice_events = 0
            op_type = "UNKNOWN"
            try:
                if dp.op_id in noc_trace_files_per_op:
                    op_info = noc_trace_files_per_op[dp.op_id]
                    op_type = "SINGLE" if not op_info.is_ccl else "MESH"
                    
                    if not op_info.is_ccl:
                        # Non-CCL operation: use dev_id specific info
                        if -1 in op_info.subdevice_events:
                            subdevice_events = op_info.subdevice_events[-1]
                    else:
                        # CCL operation: use mesh info
                        if "mesh" in op_info.subdevice_events:
                            subdevice_events = op_info.subdevice_events["mesh"]
            except (KeyError, AttributeError):
                subdevice_events = 0
                op_type = "UNKNOWN"
            
            # Handle per-device results (dp.result is now a map from device_id to result)
            # dp.result is a map/dict - write one row per device
            for device_id, device_result in dp.result.items():
                pct_total_cycles = 0 #100.0 * (device_result.golden_cycles / total_cycles)
                
                csv_writer.writerow([
                    dp.op_name,
                    dp.op_id,
                    dp.op_id,
                    device_id,
                    op_type,
                    f"{device_result.overall_avg_link_util:.2f}",
                    f"{device_result.dram_bw_util:.2f}",
                    f"{device_result.getCongestionImpact():.2f}",
                    f"{pct_total_cycles:.2f}",
                    device_result.golden_cycles,
                    subdevice_events
                ])
    
    print(f"\n📊 CSV results automatically saved to: {csv_output_path}")

def print_stats_summary_table(stats, show_accuracy_stats=False, max_display=40):
    # Print header
    print("--------------------------------------------------------------------------------------------------------------------")
    print(
            f"{BOLD}{'Opname':42} {'Op ID':>5} {'NoC Util':>14} {'DRAM BW Util':>14} {'Cong Impact':>14} {'% Overall Cycles':>19}{RESET}"
    )
    print("--------------------------------------------------------------------------------------------------------------------")

    # print data for each operation's noc trace
    sorted_events = stats.getSortedEvents()
    for i, dp in enumerate(sorted_events):
        if i >= max_display:
            remaining = len(sorted_events) - max_display
            print(f"... {remaining} operations omitted; use -m $NUM_ROWS to display more")
            break
        pct_total_cycles = 100.0 * (dp.result.golden_cycles / stats.getCycles())
        print(
                f"{dp.op_name:42} {dp.op_id:>5} {dp.result.overall_avg_link_util:>13.2f}% {dp.result.dram_bw_util:13.2f}% {dp.result.getCongestionImpact():>13.2f}% {pct_total_cycles:>18.2f}% "
        )

    print("--------------------------------------------------------------------------------------------------------------------")
    if show_accuracy_stats:
        print(f"average cycle prediction error   : {stats.getAvgError():.2f} ")
        print(f"error percentiles : ")
        for k, v in stats.getErrorPercentiles().items():
            print(f"  {k:15} : {v:4.1f}%")
    #print(f"average link util                : {stats.getAvgLinkUtil():.1f}% ")
    #print(f"cycle-weighted overall link util : {stats.getWeightedAvgLinkUtil():.1f}% ")
    #print(f"cycle-weighted dram bw util      : {stats.getWeightedAvgDramBWUtil():.1f}% ")

def extractDataFromFilename(noc_trace_file):
    basename = os.path.basename(noc_trace_file)
    match = re.search("^noc_trace_dev(\d+)_((\w*)_)?ID(\d+)\.json$", basename)
    if match:
        dev_id = int(match.group(1))
        program_runtime_id = int(match.group(4))
        op_name = match.group(3) if match.group(3) is not None else "UnknownOP"
        return dev_id, op_name, program_runtime_id
    else:
        return None, None, None

# extract dev id, opname, and opid from filename and group by aligned op ids per device
def group_traces_metal(noc_trace_files):
    # group traces per device
    noc_trace_files_per_device = {}
    for noc_trace_file in noc_trace_files:
        dev_id, op_name, program_runtime_id  = extractDataFromFilename(noc_trace_file)
        if program_runtime_id is None:
            print(f"Invalid name for noc trace file: {noc_trace_file}. Skipping...")
            continue

        assert(op_name == "UnknownOP")

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
            op_trace_files.append((dev_id, noc_trace_file))
            # use max_program_runtime_id as an op_id
            max_program_runtime_id = max(max_program_runtime_id, program_runtime_id)

        noc_trace_files_per_op[max_program_runtime_id] = OpTraces(max_program_runtime_id, "UnknownOP")
        for dev_id, trace_file in op_trace_files:
            noc_trace_files_per_op[max_program_runtime_id].insert_trace(dev_id, trace_file)

    return noc_trace_files_per_op

def get_ttnn_op_id(program_runtime_id):
    # program_runtime_id: op_id (21 bits) | device id (10 bits)
    return program_runtime_id >> 10

def get_global_call_count(op_id, dev_id):
    return op_id << 10 | dev_id

def group_traces_ttnn(noc_trace_files):
    # extract dev id, opname, and opid from filename and group by op_id
    noc_trace_files_per_op = {}
    devices_per_op = {} # to check for mutiple trace runs
    for noc_trace_file in noc_trace_files:
        dev_id, op_name, program_runtime_id  = extractDataFromFilename(noc_trace_file)
        if program_runtime_id is None:
            print(f"Invalid name for noc trace file: {noc_trace_file}. Skipping...")
            continue

        op_id = get_ttnn_op_id(program_runtime_id)

        if op_id not in devices_per_op:
            devices_per_op[op_id] = []
        # There cannot be multiple traces for a device in a single op, this likely means there are old traces present too
        if (dev_id in devices_per_op[op_id]):
            log_error("There seem to trace files from multiple runs in the trace directory, please remove old traces and run again.")
        devices_per_op[op_id].append(dev_id)

        if op_id not in noc_trace_files_per_op:
            noc_trace_files_per_op[op_id] = OpTraces(op_id, op_name)
        
        # check op name matches
        if noc_trace_files_per_op[op_id].op_name != op_name:
            log_error("Op names do not match for the same op id. There seem to trace files from multiple runs in the trace directory, please remove old traces and run again.")

        noc_trace_files_per_op[op_id].insert_trace(dev_id, noc_trace_file)

    return noc_trace_files_per_op

def sort_events(events):
    return sorted(events, key=lambda x: (x["src_device_id"], x["sx"], x["sy"], x["proc"], x["timestamp"]))

# Merge out all events from subdevice_op_events that are within start and end ts of main op
def merge_subdevice_op(main_op_trace, subdevice_op_events, quiet=False):
    0

# op id ->
# op name
# dev id -> trace file
class OpTraces:
    def __init__(self, op_id, op_name):
        self.op_id = op_id
        self.op_name = op_name
        self.traces = {} # dev id -> trace file
        self.results = {} # dev id -> trace file
        self.subdevice_events = {} # dev id -> trace file
    
    def insert_trace(self, dev_id, op_trace):
        self.traces[dev_id] = op_trace
    
    def add_npe_result(self, dev_id, result):
        if self.is_ccl: 
            self.results["mesh"] = result
        else:
            self.results[dev_id] = result

    def get_npe_result(self, dev_id):
        return self.results[dev_id]

    def merge_multichip_traces(self, noc_trace_dir, topology, quiet):
        op_trace_files = self.traces.values()
        output_file_path = os.path.join(noc_trace_dir, f"noc_trace_ID{self.op_id}_merged.json")

        self.is_ccl = True #is_ccl_op(op_trace_files, True)
        remove_desynchronized_events = True
        if (self.is_ccl):
            merge_traces(
                topology, op_trace_files, output_file_path, remove_desynchronized_events, True
            )
            self.traces.clear()
            self.traces["mesh"] = output_file_path
        # else keep as separate traces
    
    def merge_subdevice_op(self, subdevice_op_traces, quiet):
        if self.is_ccl: 
            # merge subdevice evnts into one file as well (should not be a ), otherwise merge separately
            subdevice_op_events = []
            for dev_id, trace_file in subdevice_op_traces.traces.items():
                with open(trace_file, "rb") as f:
                    events = orjson.loads(f.read())
                    log_info(
                        f"Reading subdevice op trace file '{trace_file}' ({len(events)} noc trace events) ... ",
                        True
                    )
                    subdevice_op_events.extend(events)
            
            # confirm only mesh is in self.traces!!!
            num_subdevice_events = self.merge_subdevice_op_helper(self.traces["mesh"], subdevice_op_events, True)
            if "mesh" not in self.subdevice_events:
                self.subdevice_events["mesh"] = num_subdevice_events
            else:
                self.subdevice_events["mesh"] += num_subdevice_events
            # make this a precondition???: subdevice_op_events.sort(key=lambda x: (x["src_device_id"], x["sx"], x["sy"], x["proc"], x["timestamp"]))
        else:
            for dev_id, trace_file in self.traces.items():
                # check subdevice_op_traces.traces[dev_id] exists!!!
                subdevice_op_events = []
                with open(subdevice_op_traces.traces[dev_id], "rb") as f:
                    events = orjson.loads(f.read())
                    log_info(
                        f"Reading subdevice op trace file '{trace_file}' ({len(events)} noc trace events) ... ",
                        True
                    )
                    subdevice_op_events.extend(events)
                num_subdevice_events = self.merge_subdevice_op_helper(trace_file, subdevice_op_events, True)
                if dev_id not in self.subdevice_events:
                    self.subdevice_events[dev_id] = num_subdevice_events
                else:
                    self.subdevice_events[dev_id] += num_subdevice_events
        
    def merge_subdevice_op_helper(self, main_op_trace, subdevice_op_events, quiet):
        # Read events
        main_op_events = []
        with open(main_op_trace, "rb") as f:
            events = orjson.loads(f.read())
            log_info(
                f"Reading trace file '{main_op_trace}' ({len(events)} noc trace events) ... ",
                quiet
            )
            main_op_events.extend(events)

        # Combine events
        min_ts = min(main_op_events, key=lambda x: x["timestamp"])["timestamp"]
        max_ts = max(main_op_events, key=lambda x: x["timestamp"])["timestamp"]

        # Skip merge if no overlap at all
        subdevice_max_ts = max(subdevice_op_events, key=lambda x: x["timestamp"])["timestamp"]
        if subdevice_max_ts < min_ts:
            log_debug(f"No overlap between main op and subdevice op, skipping merge", quiet)
            return 0
        
        filtered_subdevice_op_events = list(filter(lambda x: min_ts <= x["timestamp"] and x["timestamp"] <= max_ts, subdevice_op_events))
        # Get the last set state event before the op start (for each device, core, proc) in case the SET_STATE event associated with a 
        # stateful noc txn happened before the op started
        # subdevice_op_events are already ordered by (src_device_id, sx, sy, proc)
        prev_set_state_events = {}
        for i in range(len(subdevice_op_events)):
            if (subdevice_op_events[i]["timestamp"] >= min_ts):
                continue
            if subdevice_op_events[i].get("type", "").endswith("SET_STATE"):
                event = copy.deepcopy(subdevice_op_events[i])
                event["timestamp"] = min_ts - 1
                prev_set_state_events[(event["src_device_id"], event["sx"], event["sy"], event["proc"])] = event
        main_op_events.extend(filtered_subdevice_op_events)
        main_op_events.extend(prev_set_state_events.values())
        num_subdevice_events_added = len(filtered_subdevice_op_events)

        log_debug(f"Min ts of main op is {min_ts} cycles", quiet)
        log_debug(f"Min ts of main op is {max_ts} cycles", quiet)
        log_debug(f"Main op currently has currently {len(main_op_events)} events", quiet)
        log_debug(f"Adding {len(filtered_subdevice_op_events)} subdevice events", quiet)
        log_debug(f"Adding {len(prev_set_state_events.values())} subdevice events (set state events)", quiet)
        log_debug(f"New total is {len(main_op_events)} events", quiet)

        # sort back in original order
        main_op_events.sort(key=lambda x: (x["src_device_id"], x["sx"], x["sy"], x["proc"], x["timestamp"]))
        # Write combined events back to file
        log_info(f"Writing combined trace to '{main_op_trace}'", quiet)
        with open(main_op_trace, "wb") as f:
            f.write(orjson.dumps(main_op_events, option=orjson.OPT_INDENT_2))

        return num_subdevice_events_added

def analyze_noc_traces_in_dir(noc_trace_dir, emit_viz_timeline_files, compress_timeline_files=False, group_as_metal_traces = False,
        quiet=False, show_accuracy_stats=False, max_rows_in_summary_table=40): 
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
    # Create output directory and CSV path
    Path(output_dir).mkdir(parents=True, exist_ok=True)
    csv_output_path = os.path.join(output_dir, "npe_analysis_summary.csv")

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
    # keep traces separate for single-device ops (non ccls)
    # 1) identify non ccl op
    topology_file_path = os.path.join(noc_trace_dir, "topology.json")
    topology = TopologyGraph(topology_file_path)
    device_name = topology.cluster_type

    for op_id, op_traces in noc_trace_files_per_op.items():
        op_traces.merge_multichip_traces(noc_trace_dir, topology, quiet)

    # separate out sub device ops
    subdevice_ops = {}
    for op_id, op_traces in noc_trace_files_per_op.items():
        if op_traces.op_name == "DramPrefetcher":
            subdevice_ops[op_id] = op_traces

    for subdevice_op_id, subdevice_op_traces in subdevice_ops.items():
        del noc_trace_files_per_op[subdevice_op_id]

    # merge subdevice ops
    for subdevice_op_id, subdevice_op_traces in subdevice_ops.items():
        for op_id, op_traces in noc_trace_files_per_op.items():
            log_info(f"merging {op_traces.op_name} and {subdevice_op_traces.op_name}", quiet)        
            op_traces.merge_subdevice_op(subdevice_op_traces, quiet)

    log_info("Trace merging complete, running NPE next.", quiet)
    log_info("Analyzing traces...", quiet)
    stats = Stats()
    timeline_files = []
    #with Pool(processes=mp.cpu_count()) as pool:
    for op_id, op_traces in noc_trace_files_per_op.items():
        if op_traces.is_ccl:
            timeline_filepath = os.path.join(output_dir, f"{op_traces.op_name}_MESH_ID{op_id}.npeviz")
            result_data = process_trace(op_traces.traces["mesh"], device_name, topology_file_path, False, 
                emit_viz_timeline_files, timeline_filepath, compress_timeline_files)
            op_traces.add_npe_result("mesh", result_data)
        else:
            #print(op_traces.traces.items())
            for dev_id, trace_file in op_traces.traces.items():
                timeline_filepath = os.path.join(output_dir, f"{op_traces.op_name}_dev{dev_id}_ID{op_id}.npeviz")
                result_data = process_trace(trace_file, device_name, topology_file_path, True,
                    emit_viz_timeline_files, timeline_filepath, compress_timeline_files)
                op_traces.add_npe_result(dev_id, result_data)

    for op_id, op_traces in noc_trace_files_per_op.items():
        if op_traces.is_ccl:
            result_data = op_traces.get_npe_result("mesh")
            if result_data is not None:
                stats.addDatapoint(op_traces.op_name, op_id, result_data)
                timeline_files.append({"global_call_count": op_id, "file": timeline_filepath})
                #print("-------------------------", type(result_data[0]))
            #    for dev_id in range(topology.get_num_devices()):
            #        stats.addDatapoint(op_traces.op_name, get_global_call_count(op_id, dev_id), result_data)
            #        timeline_files.append({"global_call_count": get_global_call_count(op_id, dev_id), "file": timeline_filepath})
        else:
            for dev_id, trace_file in op_traces.traces.items():
                result_data = op_traces.get_npe_result(dev_id)
                if result_data is not None:
                    stats.addDatapoint(op_traces.op_name, get_global_call_count(op_id, dev_id), result_data)
                    timeline_files.append({"global_call_count": get_global_call_count(op_id, dev_id), "file": timeline_filepath})

        # run single device ops individially
        #and sollect stats for EBADMACH
        # run mesh device op as one and gather stats
        # show as hierarchical table?
        # or flattened table? 
        #        # figure out how to run optrace in paralle and pass single_device_op parameter
        #        #if multichip op, epeat same stat for mesh op adn same mesh file (with dev MESH)
        #        # otherwise use stat for each one with actual global call count adn separate npe_viz file (with dev id)
        #        #       for mesh ops could get per device stats if limitng link demand grid to that device and timesapn to that device's min and max ts???

        #process_func = partial(process_trace, device_name=device_name, topology_json_file=topology_file_path, 
        #compress_timeline_files=compress_timeline_files, output_dir=output_dir, emit_viz_timeline_files=emit_viz_timeline_files)
        #for i, result in enumerate(pool.imap_unordered(process_func, noc_trace_info)):
        #    update_message(f"Analyzing ({i + 1}/{len(noc_trace_info)}) ...", quiet)
        #    if result is not None:
        #        op_name, op_id, result_data = result
        #        stats.addDatapoint(op_name, op_id, result_data)
        #        timeline_files.append({"global_call_count": op_id, "file": op_name + "_ID" + str(op_id) + ".npeviz" 
        #            + (".zst" if compress_timeline_files else "")})
    #update_message("\n", quiet)

    # create manifest file
    if emit_viz_timeline_files:
        manifest_file_path = os.path.join(output_dir, "manifest.json")
        with open(manifest_file_path, "wb") as f:
            f.write(orjson.dumps(timeline_files, option=orjson.OPT_INDENT_2))
    
    # Generate CSV output
    if csv_output_path:
        write_csv_output(stats, csv_output_path, noc_trace_files_per_op)
    
    if not quiet:
        #print_stats_summary_table(stats, show_accuracy_stats, max_rows_in_summary_table)
        if emit_viz_timeline_files:
            print(f"\n👉 {BOLD}{GREEN}ttnn-visualizer files located in: '{output_dir}'{RESET}")
        print(f"\n👉 {BOLD}{GREEN}Output directory: '{output_dir}'{RESET}")

    return stats

def main():
    args = get_cli_args()
    analyze_noc_traces_in_dir(args.noc_trace_dir, args.emit_viz_timeline_files, args.compress_timeline_files, args.group_as_metal_traces, args.quiet, args.show_accuracy_stats, args.max_rows_in_summary_table)


if __name__ == "__main__":
    main()

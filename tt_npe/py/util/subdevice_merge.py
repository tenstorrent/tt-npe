#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

from util import log_info, log_warning, log_error, log_debug, read_trace_file_events, write_trace_file_events
import orjson
import copy

def identify_subdevice_ops(trace_info):
    subdevice_ops = {}    
    # subdevice ops should encompass other ops (and be from the same device)
    for subdevice_op_runtime_id in trace_info.keys():
        opsEncompassed = []
        for runtime_id in trace_info.keys():
            if (subdevice_op_runtime_id != runtime_id and 
                trace_info[subdevice_op_runtime_id]["dev_id"] == trace_info[runtime_id]["dev_id"]):
                # check runtime_id is completely contained in subdevice_op_runtime_id
                if (trace_info[subdevice_op_runtime_id]["min_ts"] <= trace_info[runtime_id]["min_ts"] and
                    trace_info[subdevice_op_runtime_id]["max_ts"] >= trace_info[runtime_id]["min_ts"]):
                    opsEncompassed.append(runtime_id)

        if len(opsEncompassed) >= 1:
            subdevice_ops[subdevice_op_runtime_id] = opsEncompassed

    # Check: normal ops should not overlap with other normal ops or subdevice ops that do not encompass them
    for normal_op_runtime_id in trace_info.keys():
        if normal_op_runtime_id in subdevice_ops.keys():
            continue
        for runtime_id in trace_info.keys():
            if (normal_op_runtime_id != runtime_id and 
                trace_info[normal_op_runtime_id]["dev_id"] == trace_info[runtime_id]["dev_id"]):
                if ((runtime_id not in subdevice_ops.keys() or normal_op_runtime_id not in subdevice_ops[runtime_id]) and
                    (trace_info[runtime_id]["min_ts"] <= trace_info[normal_op_runtime_id]["min_ts"] and
                    trace_info[runtime_id]["max_ts"] >= trace_info[normal_op_runtime_id]["min_ts"]) or (
                    trace_info[runtime_id]["min_ts"] >= trace_info[normal_op_runtime_id]["min_ts"] and 
                    trace_info[runtime_id]["min_ts"] <= trace_info[normal_op_runtime_id]["max_ts"])):
                    log_error(f"Partial overlap between {trace_info[runtime_id]['op_name']} (id={runtime_id}) and "
                        f"{trace_info[normal_op_runtime_id]['op_name']} (id={normal_op_runtime_id}) "
                        f"on device {trace_info[normal_op_runtime_id]['dev_id']}")

    return subdevice_ops

# Merge out all events from subdevice_op_events that are within start and end ts of main op
def merge_subdevice_op(main_op_trace, subdevice_op_events, output_file_path, quiet=False):
    # Read events
    main_op_events = read_trace_file_events(main_op_trace, quiet)

    # Combine events from subdevice_op_events that are with main op's time range
    min_ts = min(main_op_events, key=lambda x: x["timestamp"])["timestamp"]
    max_ts = max(main_op_events, key=lambda x: x["timestamp"])["timestamp"]
    filtered_subdevice_op_events = list(filter(lambda x: min_ts <= x["timestamp"] and x["timestamp"] <= max_ts, subdevice_op_events))

    # Get the last set state event before the op's start time (for each device, core, proc) 
    # in case the SET_STATE event associated with a stateful noc txn happened before 
    # the op started (subdevice_op_events are already ordered by (src_device_id, sx, sy, proc, timestamp)
    prev_set_state_events = {}
    for i in range(len(subdevice_op_events)):
        if (subdevice_op_events[i]["timestamp"] >= min_ts):
            continue
        if subdevice_op_events[i].get("type", "").endswith("SET_STATE"):
            event = copy.deepcopy(subdevice_op_events[i])
            event["timestamp"] = min_ts - 1
            prev_set_state_events[(event["src_device_id"], event["sx"], event["sy"], event["proc"])] = event
    
    # Add filtered_subdevice_op_events and prev_set_state_events to main_op_events
    log_debug(f"Min ts of main op is {min_ts} cycles", quiet)
    log_debug(f"Min ts of main op is {max_ts} cycles", quiet)
    log_debug(f"Main op currently has currently {len(main_op_events)} events", quiet)
    log_debug(f"Adding {len(filtered_subdevice_op_events)} subdevice events", quiet)
    log_debug(f"Adding {len(prev_set_state_events.values())} set state events from subdevice events", quiet)
    main_op_events.extend(filtered_subdevice_op_events)
    main_op_events.extend(prev_set_state_events.values())
    log_debug(f"New total is {len(main_op_events)} events", quiet)

    # sort back in original order
    main_op_events.sort(key=lambda x: (x["src_device_id"], x["sx"], x["sy"], x["proc"], x["timestamp"]))
    # Write combined events back to file
    write_trace_file_events(main_op_events, output_file_path, quiet)
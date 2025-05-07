#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

# -*- coding: utf-8 -*-
# --- Imports ---
import json
import random
import math
import sys
import time
import argparse
import fabric_post_process
from collections import defaultdict
from pprint import pprint

# --- Argument Parsing ---
parser = argparse.ArgumentParser(description="Generate multichip transfer data.")
parser.add_argument(
    "--num-devices", type=int, default=8, help="Number of devices in the system."
)
parser.add_argument(
    "--num-rows", type=int, default=12, help="Number of rows per device grid."
)
parser.add_argument(
    "--num-cols", type=int, default=10, help="Number of columns per device grid."
)
parser.add_argument(
    "--num-transfers",
    type=int,
    default=32,
    help="Total number of transfers to generate.",
)
parser.add_argument(
    "--cycles-per-timestep",
    type=int,
    default=32,
    help="Granularity of timestep data in cycles.",
)
parser.add_argument(
    "--transfer-density-per-timestep",
    type=float,
    default=4,
    help="Average number of transfers starting per timestep.",
)
parser.add_argument(
    "--output-file", type=str, default="output.json", help="Output file name."
)
parser.add_argument(
    "--no-minify-json-output",
    action="store_true",
    default=False,
    help="Do not minify JSON output.",
)

args = parser.parse_args()

# --- Derived Configuration ---
MAX_ROW = args.num_rows - 1  # Max row index based on args
MAX_COL = args.num_cols - 1  # Max col index based on args
SIMULATION_CYCLES = (
    args.num_transfers / args.transfer_density_per_timestep
) * args.cycles_per_timestep  # Based on args
print("simulation cycles: ", SIMULATION_CYCLES)

# This is *approximately* a T3K
chips = {
    # device_x, device_y, rack, shelf
    0: [1, 0, 0, 0],
    1: [1, 1, 0, 0],
    2: [2, 1, 0, 0],
    3: [2, 0, 0, 0],
    4: [0, 0, 0, 0],
    5: [0, 1, 0, 0],
    6: [3, 1, 0, 0],
    7: [3, 0, 0, 0],
}

eth_channel_to_coord = {
    0: (9, 0),
    1: (1, 0),
    2: (8, 0),
    3: (2, 0),
    4: (7, 0),
    5: (3, 0),
    6: (6, 0),
    7: (4, 0),
    8: (9, 6),
    9: (1, 6),
    10: (8, 6),
    11: (2, 6),
    12: (7, 6),
    13: (3, 6),
    14: (6, 6),
    15: (4, 6),
}

# WORKER_CORES generation - uses fixed ranges but checks against grid boundaries from args
WORKER_CORES = set()
for row in range(1, 5):
    for col in range(1, 4):
        if row < args.num_rows and col < args.num_cols:  # Check bounds
            WORKER_CORES.add((row, col))
    # Cols 6-8
    for col in range(6, 9):
        if row < args.num_rows and col < args.num_cols:  # Check bounds
            WORKER_CORES.add((row, col))
for row in range(7, 10):
    for col in range(1, 4):
        if row < args.num_rows and col < args.num_cols:  # Check bounds
            WORKER_CORES.add((row, col))
    # Cols 6-8
    for col in range(6, 9):
        if row < args.num_rows and col < args.num_cols:  # Check bounds
            WORKER_CORES.add((row, col))

DRAM_CORES = set()
for row in range(1, 11):
    col = 5
    DRAM_CORES.add((row, col))

# --- Helper Functions ---

def is_dram_core(coord):
    device_id, row, col = coord
    return (row, col) in DRAM_CORES

def get_random_worker_or_dram_core(device_id, chance_of_dram=0.25):
    """Generates a random worker core or DRAM core coordinate [device_id, row, col]."""
    if random.random() < chance_of_dram:
        dram_row, dram_col = random.choice(list(DRAM_CORES))
        return (device_id, dram_row, dram_col)
    else:
        worker_row, worker_col = random.choice(list(WORKER_CORES))
        return (device_id, worker_row, worker_col)


def generate_route_segment(device_id, start_coord, end_coord, noc_type):
    """
    Generates a single route segment on a specific device using specific
    dimension-ordered routing rules on a 2D TORUS.

    Args:
        device_id (int): The device ID for this segment.
        start_coord (list): The starting coordinate [device, row, col].
        end_coord (list): The ending coordinate [device, row, col].
        noc_type (str): Either "NOC0" or "NOC1".

    Returns:
        tuple: A tuple containing:
            - list: The list of links [[row, col, link_type], ...].
            - list: The final destination coordinate reached [device, row, col].
                    (Should always match end_coord on torus).
    """
    links = []
    current_row, current_col = start_coord[1], start_coord[2]
    end_row, end_col = end_coord[1], end_coord[2]

    if current_row == end_row and current_col == end_col:
        return [], [device_id, current_row, current_col]  # No movement needed

    if noc_type == "NOC0":
        # Rule: EAST then SOUTH on Torus
        # Move EAST (potentially wrapping around)
        while current_col != end_col:
            link_row, link_col = current_row, current_col
            link_type = "NOC0_EAST"
            links.append([device_id, link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_col == MAX_COL:
                current_col = 0  # Wrap EAST
            else:
                current_col += 1

        # Move SOUTH (potentially wrapping around) - Column is now aligned
        while current_row != end_row:
            link_row, link_col = current_row, current_col  # Use the aligned column
            link_type = "NOC0_SOUTH"
            links.append([device_id, link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_row == MAX_ROW:
                current_row = 0  # Wrap SOUTH
            else:
                current_row += 1

    elif noc_type == "NOC1":
        # Rule: NORTH then WEST on Torus
        # Move NORTH (potentially wrapping around)
        while current_row != end_row:
            link_row, link_col = current_row, current_col
            link_type = "NOC1_NORTH"
            links.append([device_id, link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_row == 0:
                current_row = MAX_ROW  # Wrap NORTH
            else:
                current_row -= 1

        # Move WEST (potentially wrapping around) - Row is now aligned
        while current_col != end_col:
            link_row, link_col = current_row, current_col  # Use the aligned row
            link_type = "NOC1_WEST"
            links.append([device_id, link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_col == 0:
                current_col = MAX_COL  # Wrap WEST
            else:
                current_col -= 1
    else:
        print(f"Error: Unknown noc_type '{noc_type}'")
        return [], [
            device_id,
            start_coord[1],
            start_coord[2],
        ]  # Return start coord on error

    # Final coordinate reached should match the target end coordinate
    segment_dst = [device_id, current_row, current_col]

    # Sanity check (optional)
    if current_row != end_row or current_col != end_col:
        print(
            f"ERROR: Torus routing failed for {noc_type} from {start_coord} to {end_coord}. Ended at {segment_dst}"
        )
        # This should ideally not happen with the torus logic

    return links, segment_dst


def estimate_transfer_time(total_bytes, num_links, injection_rate):
    """Estimates transfer time based on bytes and injection rate."""
    if not injection_rate:
        injection_rate = 1.0  # Prevent division by zero

    transfer_cycles = total_bytes / injection_rate

    # Ensure minimum time is at least 40 cycles
    return max(40, int(transfer_cycles))


# --- Main Data Generation ---

noc_transfers = []
all_links_used = {}  # Store link usage per cycle for timestep aggregation

curr_max_cycle = 0

route_finder = fabric_post_process.TopologyGraph("./workload/T3K-fabric-traces/noc_traces/line-8dev/topology.json")

start_time = time.time()
for i in range(args.num_transfers):
    transfer_id = i
    noc_event_type = random.choice(["READ", "WRITE"])
    total_bytes = random.choice([2048, 4096, 8192])
    injection_rate = 28.1
    route = []
    transfer_start_cycle = 0
    transfer_end_cycle = 0
    final_dst_coord = None  # Keep track of the intended final destination

    # Decide if intra-device or inter-device
    is_inter_device = (
        random.random() < 0.6 and args.num_devices > 1
    )  # 30% chance & multiple devices exist
    src_device = random.randrange(args.num_devices)

    if is_inter_device:

        LINK_LATENCY = 1200
        potential_dst_device = [i for i in range(args.num_devices) if i != src_device]
        dst_device = random.choice(potential_dst_device)

        # all traffic between devices is worker to worker
        src_coord = get_random_worker_or_dram_core(src_device, 0)
        dst_coord = get_random_worker_or_dram_core(dst_device, 0)

        final_dst_coord = [dst_coord]  # Store intended final destination

        ptp_path_to_dst = route_finder.find_optimal_path(src_coord, dst_coord, src_device, dst_device)

        start_cycle = random.randrange(0, int(SIMULATION_CYCLES * 0.9))
        for waypoint in ptp_path_to_dst:
            device_id = waypoint['device']
            segment_start_x = waypoint['segment_start_x']
            segment_start_y = waypoint['segment_start_y']
            segment_end_x = waypoint['segment_end_x']
            segment_end_y = waypoint['segment_end_y']

            noc_type = random.choice(["NOC0", "NOC1"])
            links, seg_dst_coord = generate_route_segment(
                device_id, [device_id, segment_start_x, segment_start_y], [device_id, segment_end_x, segment_end_y], noc_type
            )

            est_time = estimate_transfer_time(
                total_bytes, len(links) if links else 1, injection_rate
            )  # Handle zero links
            end_cycle = start_cycle + est_time

            curr_max_cycle = max(curr_max_cycle, end_cycle)

            segment = {
                "device_id": device_id,
                "src": [device_id, segment_start_x, segment_start_y],
                "dst": [device_id, segment_end_x, segment_end_y],  # Destination is the gateway core reached
                "noc_type": noc_type,
                "injection_rate": injection_rate,
                "start_cycle": start_cycle,
                "end_cycle": end_cycle,
                "links": links,
            }
            route.append(segment)

            start_cycle += LINK_LATENCY

        #print(f"\nRoute from {src_coord} to {final_dst_coord}:")
        #pprint(route)

    else:  # Intra-device transfer
        src_coord = get_random_worker_or_dram_core(src_device)
        # Ensure dst is on the same device but different core
        while True:
            # ensure that DRAM<->DRAM traffic is not generated
            if is_dram_core(src_coord):
                dst_coord = get_random_worker_or_dram_core(src_device, 0)
            else:
                dst_coord = get_random_worker_or_dram_core(src_device)
            if src_coord != dst_coord:
                break
        final_dst_coord = [dst_coord]  # Store intended final destination

        # Randomly choose NOC type for the segment
        noc_type = random.choice(["NOC0", "NOC1"])
        links, seg_dst_coord = generate_route_segment(
            src_device, src_coord, dst_coord, noc_type
        )

        start_cycle = random.randrange(0, int(SIMULATION_CYCLES * 0.9))
        est_time = estimate_transfer_time(
            total_bytes, len(links) if links else 1, injection_rate
        )  # Handle zero links
        end_cycle = start_cycle + est_time

        curr_max_cycle = max(curr_max_cycle, end_cycle)

        segment = {
            "device_id": src_device,
            "src": src_coord,
            "dst": [seg_dst_coord],  # Actual destination reached by segment
            "noc_type": noc_type,
            "injection_rate": injection_rate,
            "start_cycle": start_cycle,
            "end_cycle": end_cycle,
            "links": links,
        }
        route = [segment]
        transfer_start_cycle = start_cycle
        transfer_end_cycle = end_cycle
        # Note: final_dst_coord remains the *intended* destination

    # Add the transfer (routing should always be possible now)
    transfer = {
        "id": transfer_id,
        "noc_event_type": noc_event_type,
        "total_bytes": total_bytes,
        "src": route[0]["src"],  # Use the actual source from the first segment
        "dst": final_dst_coord,  # Use the originally intended destination(s)
        "start_cycle": transfer_start_cycle,
        "end_cycle": transfer_end_cycle,
        "route": route,  # The generated route segments
    }
    noc_transfers.append(transfer)

    # Record link usage for timestep aggregation
    for segment in route:
        seg_start = segment["start_cycle"]
        seg_end = segment["end_cycle"]
        dev_id = segment["device_id"]

        # Avoid division by zero if segment duration is zero or negative, or no links
        duration = seg_end - seg_start + 1
        num_seg_links = len(segment["links"])
        if duration <= 0 or num_seg_links == 0:
            continue  # Skip link usage for zero-duration/empty segments

        # Simplification: calculate demand contribution per link
        approx_demand_per_link = total_bytes / duration

        for link in segment["links"]:
            # link format [row, col, link_type]
            link_key = (link[0], link[1], link[2], link[3])
            if link_key not in all_links_used:
                all_links_used[link_key] = []
            # Record cycle range and approximate demand contribution
            all_links_used[link_key].append(
                {
                    "start": seg_start,
                    "end": seg_end,
                    "demand": approx_demand_per_link,
                    "id": transfer_id,
                }
            )

# --- Generate Timestep Data ---
print("Binning link usages by timestep...")
timestep_link_bins = defaultdict(list)
timestep_transfer_bins = defaultdict(set)
for link_key, usages in all_links_used.items():
    for usage in usages:
        start_ts = usage["start"] // args.cycles_per_timestep
        end_ts = (usage["end"] - 1) // args.cycles_per_timestep

        # Add this usage's info to all timesteps it overlaps with
        for t in range(start_ts, end_ts + 1):
            timestep_link_bins[t].append((link_key, usage["demand"]))
            timestep_transfer_bins[t].add(usage["id"])

timestep_data = []
actual_max_cycle = max(t["end_cycle"] for t in noc_transfers)
effective_simulation_cycles = (
    max(curr_max_cycle, actual_max_cycle) + args.cycles_per_timestep
)  # Add buffer
num_timesteps = math.ceil(effective_simulation_cycles / args.cycles_per_timestep)

# compute total number of links on the device, each XY has location has 8 links total
num_link_types_per_noc = 4
num_links_per_noc = args.num_rows * args.num_cols * num_link_types_per_noc
num_links = 2 * num_links_per_noc

print("Generating timestep data...")
max_link_bw = 28.1
for t in range(num_timesteps):
    start_cycle = t * args.cycles_per_timestep
    end_cycle = (t + 1) * args.cycles_per_timestep

    link_demand_aggregated = {}
    for link_key, demand in timestep_link_bins[t]:
        if link_key not in link_demand_aggregated:
            link_demand_aggregated[link_key] = 0
        link_demand_aggregated[link_key] += demand

    total_link_demand = sum(demand for demand in link_demand_aggregated.values())
    total_link_demand *= (
        100 / max_link_bw
    )  # normalize to percentage of max link bandwidth
    avg_link_demand = round((total_link_demand / num_links), 2)

    # for any given link, the max utilization is max_link_bw
    total_link_util = sum(
        min(max_link_bw, demand) for demand in link_demand_aggregated.values()
    )
    total_link_util *= (
        100 / max_link_bw
    )  # normalize to percentage of max link bandwidth
    avg_link_util = round((total_link_util / num_links), 2)

    link_demand = []
    for link_key, demand in link_demand_aggregated.items():
        link_demand.append(
            [link_key[0], link_key[1], link_key[2], link_key[3], round(demand, 2)]
        )
    link_demand.sort()

    # compute per noc link demand and util
    noc_0_link_demand = 0
    noc_1_link_demand = 0
    noc_0_link_util = 0
    noc_1_link_util = 0
    for link_key, demand in link_demand_aggregated.items():
        pct_demand = (100 / max_link_bw) * demand
        if link_key[3].startswith("NOC0"):
            noc_0_link_demand += pct_demand
            noc_0_link_util += min(100, pct_demand)
        elif link_key[3].startswith("NOC1"):
            noc_1_link_demand += pct_demand
            noc_1_link_util += min(100, pct_demand)
        else:
            print("unknown noc type: " + link_key[3])

    noc_0_avg_link_demand = round((noc_0_link_demand / num_links_per_noc), 2)
    noc_0_avg_link_util = round((noc_0_link_util / num_links_per_noc), 2)
    noc_1_avg_link_demand = round((noc_1_link_demand / num_links_per_noc), 2)
    noc_1_avg_link_util = round((noc_1_link_util / num_links_per_noc), 2)

    timestep_data.append(
        {
            "start_cycle": start_cycle,
            "end_cycle": end_cycle,
            "active_transfers": list(timestep_transfer_bins[t]),
            "link_demand": link_demand,
            "avg_link_demand": avg_link_demand,
            "avg_link_util": avg_link_util,
            "noc": {
                "NOC0": {
                    "avg_link_demand": noc_0_avg_link_demand,
                    "avg_link_util": noc_0_avg_link_util,
                },
                "NOC1": {
                    "avg_link_demand": noc_1_avg_link_demand,
                    "avg_link_util": noc_1_avg_link_util,
                },
            },
        }
    )

# compute overall avg and max link demand and util by iterating over timestep_data
overall_link_demand = 0
overall_link_util = 0
overall_noc_0_link_demand = 0
overall_noc_0_link_util = 0
overall_noc_1_link_demand = 0
overall_noc_1_link_util = 0
overall_noc_0_max_link_demand = 0
overall_noc_1_max_link_demand = 0
max_link_demand = 0
for t in timestep_data:
    overall_link_demand += t["avg_link_demand"]
    overall_link_util += t["avg_link_util"]
    max_link_demand = max(max_link_demand, t["avg_link_demand"])
    overall_noc_0_link_demand += t["noc"]["NOC0"]["avg_link_demand"]
    overall_noc_0_link_util += t["noc"]["NOC0"]["avg_link_util"]
    overall_noc_1_link_demand += t["noc"]["NOC1"]["avg_link_demand"]
    overall_noc_1_link_util += t["noc"]["NOC1"]["avg_link_util"]
    overall_noc_0_max_link_demand = max(
        overall_noc_0_max_link_demand, t["noc"]["NOC0"]["avg_link_demand"]
    )
    overall_noc_1_max_link_demand = max(
        overall_noc_1_max_link_demand, t["noc"]["NOC1"]["avg_link_demand"]
    )

overall_link_demand_avg = round((overall_link_demand / len(timestep_data)), 2)
overall_link_util_avg = round((overall_link_util / len(timestep_data)), 2)
overall_noc_0_link_demand_avg = round(
    (overall_noc_0_link_demand / len(timestep_data)), 2
)
overall_noc_0_link_util_avg = round((overall_noc_0_link_util / len(timestep_data)), 2)
overall_noc_1_link_demand_avg = round(
    (overall_noc_1_link_demand / len(timestep_data)), 2
)
overall_noc_1_link_util_avg = round((overall_noc_1_link_util / len(timestep_data)), 2)
overall_noc_0_max_link_demand_avg = round(overall_noc_0_max_link_demand, 2)
overall_noc_1_max_link_demand_avg = round(overall_noc_1_max_link_demand, 2)

# --- Combine and Output ---
common_info = {
    "version": "1.0.0",  # Updated version
    "cycles_per_timestep": args.cycles_per_timestep,
    "congestion_model_name": "fast",
    "mesh_device": f"T3K",
    "arch": f"wormhole_b0",
    "dram_bw_util": round(random.uniform(10.0, 35.0), 1),
    "link_util": overall_link_util_avg,
    "link_demand": overall_link_demand_avg,
    "max_link_demand": max_link_demand,
    "noc": {
        "NOC0": {
            "avg_link_demand": overall_noc_0_link_demand_avg,
            "avg_link_util": overall_noc_0_link_util_avg,
            "max_link_demand": overall_noc_0_max_link_demand_avg,
        },
        "NOC1": {
            "avg_link_demand": overall_noc_1_link_demand_avg,
            "avg_link_util": overall_noc_1_link_util_avg,
            "max_link_demand": overall_noc_1_max_link_demand_avg,
        },
    },
}

final_data = {
    "common_info": common_info,
    "chips": chips,
    "noc_transfers": noc_transfers,
    "timestep_data": timestep_data,
}

# --- Final Report ---
with open(args.output_file, "w") as f:
    if args.no_minify_json_output:
        f.write(json.dumps(final_data, indent=2))
    else:
        f.write(json.dumps(final_data, separators=(",", ":")))

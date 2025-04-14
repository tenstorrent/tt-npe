#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

# -*- coding: utf-8 -*-
# --- Imports ---
import json
import random
import math
import time

# --- Configuration ---
NUM_DEVICES = 8
NUM_ROWS = 12
NUM_COLS = 10
MAX_ROW = NUM_ROWS - 1 # Max row index
MAX_COL = NUM_COLS - 1 # Max col index

NUM_TRANSFERS = 32  # How many transfers to generate
CYCLES_PER_TIMESTEP = 128 # Granularity of timestep data
TRANSFER_DENSITY_PER_TIMESTEP = 4
SIMULATION_CYCLES = (NUM_TRANSFERS / TRANSFER_DENSITY_PER_TIMESTEP) * CYCLES_PER_TIMESTEP

# Gateway core locations (row 6, cols 1, 2, 3, 4, 6, 7, 8, 9)
GATEWAY_COLS = {1, 2, 3, 4, 6, 7, 8, 9}
GATEWAY_ROW = 6
GATEWAY_CORES = {(GATEWAY_ROW, col) for col in GATEWAY_COLS}

WORKER_CORES = set()
for row in range(1, 5):
    for col in range(1, 4):
        WORKER_CORES.add((row, col))
    for col in range(6,9): 
        WORKER_CORES.add((row, col))
for row in range(7, 10):
    for col in range(1, 4):
        WORKER_CORES.add((row, col))
    for col in range(6,9): 
        WORKER_CORES.add((row, col))

# --- Helper Functions ---

def get_random_core(device_id=None):
    """Generates a random core coordinate [device_id, row, col]."""
    if device_id is None:
        device_id = random.randrange(NUM_DEVICES)
    row = random.randrange(NUM_ROWS)
    col = random.randrange(NUM_COLS)
    return [device_id, row, col]

def get_random_worker_core(device_id=None):
    is_worker_core = lambda coord: (coord[1], coord[2]) in WORKER_CORES
    worker_core = [0,0,0]
    while not is_worker_core(worker_core):
        worker_core = get_random_core(device_id)
    return worker_core

def get_gateway_core(src_device_id, dst_device_id):
    """Gets the gateway core location based on source and destination device IDs."""
    diff = src_device_id - dst_device_id

    if diff == 4:
        # Positive vertical hop
        return [src_device_id, 6, 1]
    elif diff == -4:
        # Negative vertical hop
        return [src_device_id, 6, 6]
    elif diff == 1:
        # Positive horizontal hop
        return [src_device_id, 6, 2]
    elif diff == -1:
        # Negative horizontal hop
        return [src_device_id, 6, 7]
    else:
        raise ValueError(
            f"Destination device {dst_device_id} unreachable from source device {src_device_id} with 2 hops"
        )

def generate_route_segment(device_id, start_coord, end_coord, noc_type):
    """
    Generates a single route segment on a specific device using specific
    dimension-ordered routing rules on a 2D TORUS.

    Args:
        device_id (int): The device ID for this segment.
        start_coord (list): The starting coordinate [device, row, col].
        end_coord (list): The ending coordinate [device, row, col].
        noc_type (str): Either "NOC_0" or "NOC_1".

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
        return [], [device_id, current_row, current_col] # No movement needed

    if noc_type == "NOC_0":
        # Rule: EAST then SOUTH on Torus
        # Move EAST (potentially wrapping around)
        while current_col != end_col:
            link_row, link_col = current_row, current_col
            link_type = "NOC_0_EAST"
            links.append([link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_col == MAX_COL:
                current_col = 0 # Wrap EAST
            else:
                current_col += 1

        # Move SOUTH (potentially wrapping around) - Column is now aligned
        while current_row != end_row:
            link_row, link_col = current_row, current_col # Use the aligned column
            link_type = "NOC_0_SOUTH"
            links.append([link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_row == MAX_ROW:
                current_row = 0 # Wrap SOUTH
            else:
                current_row += 1

    elif noc_type == "NOC_1":
        # Rule: NORTH then WEST on Torus
        # Move NORTH (potentially wrapping around)
        while current_row != end_row:
            link_row, link_col = current_row, current_col
            link_type = "NOC_1_NORTH"
            links.append([link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_row == 0:
                current_row = MAX_ROW # Wrap NORTH
            else:
                current_row -= 1

        # Move WEST (potentially wrapping around) - Row is now aligned
        while current_col != end_col:
            link_row, link_col = current_row, current_col # Use the aligned row
            link_type = "NOC_1_WEST"
            links.append([link_row, link_col, link_type])
            # Update coordinate *after* adding link
            if current_col == 0:
                current_col = MAX_COL # Wrap WEST
            else:
                current_col -= 1
    else:
        print(f"Error: Unknown noc_type '{noc_type}'")
        return [], [device_id, start_coord[1], start_coord[2]] # Return start coord on error

    # Final coordinate reached should match the target end coordinate
    segment_dst = [device_id, current_row, current_col]

    # Sanity check (optional)
    if current_row != end_row or current_col != end_col:
         print(f"ERROR: Torus routing failed for {noc_type} from {start_coord} to {end_coord}. Ended at {segment_dst}")
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

common_info = {
    "version": "1.0", # Updated version
    "cycles_per_timestep": CYCLES_PER_TIMESTEP,
    "congestion_model_name": "fast",
    "device_name": f"t3k_wh",
    "dram_bw_util": round(random.uniform(10.0, 35.0), 1),
    "link_util": round(random.uniform(5.0, 20.0), 1),
    "link_demand": round(random.uniform(10.0, 30.0), 1),
    "max_link_demand": round(random.uniform(25.0, 50.0), 1)
}

noc_transfers = []
all_links_used = {} # Store link usage per cycle for timestep aggregation

curr_max_cycle = 0

start_time = time.time()
for i in range(NUM_TRANSFERS):
    transfer_id = i
    noc_event_type = random.choice(["READ", "WRITE"])
    total_bytes = random.choice([2048, 4096, 8192])
    injection_rate = 28.1

    route = []
    transfer_start_cycle = 0
    transfer_end_cycle = 0
    final_dst_coord = None # Keep track of the intended final destination

    # Decide if intra-device or inter-device
    is_inter_device = random.random() < 0.6 and NUM_DEVICES > 1 # 30% chance & multiple devices exist
    src_device = random.randrange(NUM_DEVICES)

    if is_inter_device:
        # --- Calculate valid destinations based on 2-hop reachability ---
        potential_dst = []
        # Horizontal neighbors
        if src_device + 1 < NUM_DEVICES:
            potential_dst.append(src_device + 1)
        if src_device - 1 >= 0:
            potential_dst.append(src_device - 1)
        # Vertical neighbors
        if src_device + 4 < NUM_DEVICES:
             potential_dst.append(src_device + 4)
        if src_device - 4 >= 0:
             potential_dst.append(src_device - 4)

        if not potential_dst:
            # Should not happen if num_devices allows neighbors, but handle defensively
            # print(f"Warning: No routable destination found for src_device {src_device}. Skipping attempt.")
            continue # Try generating a different src_device

        dst_device = random.choice(potential_dst)
        # --- End destination calculation ---

        src_coord = get_random_worker_core(src_device)
        dst_coord = get_random_worker_core(dst_device)
        final_dst_coord = [dst_coord] # Store intended final destination

        # Route needs two segments: src -> gateway on src_dev, gateway -> dst on dst_dev
        src_gateway = get_gateway_core(src_device, dst_device)
        dst_gateway = get_gateway_core(dst_device, src_device) # Corresponding gateway on dst device

        # --- Segment 1: src -> src_gateway ---
        # Randomly choose NOC type for the segment
        noc_type_1 = random.choice(["NOC_0", "NOC_1"])
        links1, seg1_dst_coord = generate_route_segment(src_device, src_coord, src_gateway, noc_type_1)

        start_cycle_1 = random.randrange(0, int(SIMULATION_CYCLES * 0.9))
        est_time_1 = estimate_transfer_time(total_bytes, len(links1) if links1 else 1, injection_rate) # Handle zero links
        end_cycle_1 = start_cycle_1 + est_time_1

        curr_max_cycle = max(curr_max_cycle, end_cycle_1)

        segment1 = {
            "device_id": src_device,
            "src": src_coord,
            "dst": [seg1_dst_coord], # Destination is the gateway core reached
            "noc_type": noc_type_1,
            "injection_rate": injection_rate,
            "start_cycle": start_cycle_1,
            "end_cycle": end_cycle_1,
            "links": links1
        }
        route.append(segment1)

        # --- Segment 2: dst_gateway -> dst ---
        inter_device_latency = 500 
        start_cycle_2 = end_cycle_1 + inter_device_latency
        curr_max_cycle = max(curr_max_cycle, start_cycle_2) 

        # Randomly choose NOC type for the segment
        noc_type_2 = random.choice(["NOC_0", "NOC_1"])
        # Source for segment 2 is the *destination* gateway core
        links2, seg2_dst_coord = generate_route_segment(dst_device, dst_gateway, dst_coord, noc_type_2)

        est_time_2 = estimate_transfer_time(total_bytes, len(links2) if links2 else 1, injection_rate) # Handle zero links
        end_cycle_2 = start_cycle_2 + est_time_2

        segment2 = {
            "device_id": dst_device,
            "src": dst_gateway, # Starting from the gateway on the destination device
            "dst": [seg2_dst_coord], # Final destination reached
            "noc_type": noc_type_2,
            "injection_rate": injection_rate,
            "start_cycle": start_cycle_2,
            "end_cycle": end_cycle_2,
            "links": links2
        }
        route.append(segment2)
        transfer_start_cycle = segment1["start_cycle"]
        transfer_end_cycle = segment2["end_cycle"]
        # Note: final_dst_coord remains the *intended* destination for the transfer object

    else: # Intra-device transfer
        src_coord = get_random_core(src_device)
        # Ensure dst is on the same device but different core
        while True:
            dst_coord = get_random_core(src_device)
            if src_coord != dst_coord:
                break
        final_dst_coord = [dst_coord] # Store intended final destination

        # Randomly choose NOC type for the segment
        noc_type = random.choice(["NOC_0", "NOC_1"])
        links, seg_dst_coord = generate_route_segment(src_device, src_coord, dst_coord, noc_type)

        start_cycle = random.randrange(0, int(SIMULATION_CYCLES * 0.9))
        est_time = estimate_transfer_time(total_bytes, len(links) if links else 1, injection_rate) # Handle zero links
        end_cycle = start_cycle + est_time

        curr_max_cycle = max(curr_max_cycle, end_cycle)

        segment = {
            "device_id": src_device,
            "src": src_coord,
            "dst": [seg_dst_coord], # Actual destination reached by segment
            "noc_type": noc_type,
            "injection_rate": injection_rate,
            "start_cycle": start_cycle,
            "end_cycle": end_cycle,
            "links": links
        }
        route = [segment]
        transfer_start_cycle = start_cycle
        transfer_end_cycle = end_cycle
        # Note: final_dst_coord remains the *intended* destination

    # Add the transfer (routing should always be possible now)
    transfer = {
        "transfer_id": transfer_id,
        "noc_event_type": noc_event_type,
        "total_bytes": total_bytes,
        "src": route[0]['src'], # Use the actual source from the first segment
        "dst": final_dst_coord, # Use the originally intended destination(s)
        "start_cycle": transfer_start_cycle,
        "end_cycle": transfer_end_cycle,
        "route": route # The generated route segments
    }
    noc_transfers.append(transfer)

    # Record link usage for timestep aggregation
    for segment in route:
        seg_start = segment["start_cycle"]
        seg_end = segment["end_cycle"]
        dev_id = segment["device_id"]

        # Avoid division by zero if segment duration is zero or negative, or no links
        duration = seg_end - seg_start + 1
        num_seg_links = len(segment['links'])
        if duration <= 0 or num_seg_links == 0:
             continue # Skip link usage for zero-duration/empty segments

        # Simplification: calculate demand contribution per link
        approx_demand_per_link = (total_bytes / duration) 

        for link in segment["links"]:
            # link format [row, col, link_type]
            link_key = (dev_id, link[0], link[1], link[2])
            if link_key not in all_links_used:
                all_links_used[link_key] = []
            # Record cycle range and approximate demand contribution
            all_links_used[link_key].append({'start': seg_start, 'end': seg_end, 'demand': approx_demand_per_link, 'transfer_id': transfer_id})


# --- Generate Timestep Data ---
timestep_data = []
# Adjust simulation cycles based on the latest end time of any transfer if needed
actual_max_cycle = 0
if noc_transfers:
    actual_max_cycle = max(t['end_cycle'] for t in noc_transfers)
effective_simulation_cycles = max(curr_max_cycle, actual_max_cycle) + CYCLES_PER_TIMESTEP # Add buffer

num_timesteps = math.ceil(effective_simulation_cycles / CYCLES_PER_TIMESTEP)
end_time = time.time()
print(f"Transfer generation took {end_time - start_time:.2f} seconds")

timestep_gen_start_time = time.time()
for ts in range(num_timesteps):
    ts_start_cycle = ts * CYCLES_PER_TIMESTEP
    ts_end_cycle = (ts + 1) * CYCLES_PER_TIMESTEP
    # ts_end_cycle = min((ts + 1) * CYCLES_PER_TIMESTEP, effective_simulation_cycles) # Use if exact end needed
    if ts_start_cycle >= ts_end_cycle: continue

    active_transfers_in_ts = set()
    ts_link_demand = {} # Key: link_key, Value: total demand in this timestep
    processed_links_in_ts = set() # Track links aggregated in this timestep
    total_demand_in_ts = 0
    num_links_in_ts = 0


    # Find active transfers and calculate link demand for this timestep
    for link_key, usages in all_links_used.items():
        link_demand_in_ts = 0
        link_active_in_ts = False
        for usage in usages:
             # Check if the usage period of this link overlaps with the current timestep
             overlap_start = max(usage['start'], ts_start_cycle)
             overlap_end = min(usage['end'], ts_end_cycle)

             if overlap_start < overlap_end: # Check for actual overlap
                 # Simplification: Add the usage demand if active in this timestep
                 # Weight demand by overlap duration? Maybe too complex for synthetic.
                 link_demand_in_ts += usage['demand']
                 link_active_in_ts = True
                 # Make sure the transfer using it is marked active
                 active_transfers_in_ts.add(usage['transfer_id'])

        if link_active_in_ts:
             # Normalize demand to be %-like (e.g., relative to link capacity - assume capacity is ~32 B/cycle?)
             capacity_guess = 32.0
             demand_percent = min((link_demand_in_ts / capacity_guess) * 100, 300.0) # Cap demand %
             ts_link_demand[link_key] = round(demand_percent, 1)
             if link_key not in processed_links_in_ts:
                 total_demand_in_ts += demand_percent
                 num_links_in_ts += 1
                 processed_links_in_ts.add(link_key)


    avg_demand_ts = round(total_demand_in_ts / num_links_in_ts, 1) if num_links_in_ts > 0 else 0.0
    # Utilization is demand capped at 100% (very simplified)
    avg_util_ts = round(sum(min(d, 100.0) for d in ts_link_demand.values()) / num_links_in_ts, 1) if num_links_in_ts > 0 else 0.0

    # Format link_demand for JSON output
    formatted_link_demand = [
        [key[0], key[1], key[2], key[3], demand]
        for key, demand in sorted(ts_link_demand.items()) # Sort for consistent output
    ]

    timestep_entry = {
        "start_cycle": ts_start_cycle,
        "end_cycle": ts_end_cycle,
        "active_transfers": sorted(list(active_transfers_in_ts)),
        "avg_demand": avg_demand_ts,
        "avg_util": avg_util_ts,
        "link_demand": formatted_link_demand
    }
    # Only add timestep if there was activity (active transfers or link demand)
    if timestep_entry["active_transfers"] or timestep_entry["link_demand"]:
        timestep_data.append(timestep_entry)

timestep_gen_end_time = time.time()
print(f"Timestep generation took {timestep_gen_end_time - timestep_gen_start_time:.2f} seconds")

# --- Combine and Output ---

final_data = {
    "common_info": common_info,
    "noc_transfers": noc_transfers,
    "timestep_data": timestep_data
}

# --- Final Report ---
with open("output.json", "w") as f:
    f.write(json.dumps(final_data, separators=(',', ':')))
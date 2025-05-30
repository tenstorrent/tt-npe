#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

import glob
import os
import re
from typing import Dict, List, Set, Tuple, DefaultDict
from collections import defaultdict
from dataclasses import dataclass
import orjson
import sys


def strip_comments(json_str: str) -> str:
    """Strip JavaScript-style comments from JSON string"""
    # First remove single-line comments (//...)
    json_str = re.sub(r"//.*$", "", json_str, flags=re.MULTILINE)
    # Then remove multi-line comments (/* ... */)
    json_str = re.sub(r"/\*.*?\*/", "", json_str, flags=re.DOTALL)
    return json_str


@dataclass
class EDMInfo:
    local_device_id: int
    local_eth_chan: int
    paired_remote_device_id: int
    paired_remote_chan: int


class TopologyGraph:
    def __init__(self, topology_file: str):
        """Initialize topology graph from topology.json file"""
        with open(topology_file, "r") as f:
            # Strip comments before parsing JSON
            json_str = strip_comments(f.read())
            topology = orjson.loads(json_str)

        # mapping from (device_id, eth_chan) to EDMInfo
        self.edm_map: Dict[(int, int), EDMInfo] = {}
        self.fwd_chan_lookup: Dict[(int, int), int] = {}
        self.eth_chan_to_coord: Dict[int, Tuple[int, int]] = {}

        # Build forwarding map from "forwarding_channel_pairs"
        for fwd_pair_info in topology.get("forwarding_channel_pairs", []):
            device_id = fwd_pair_info.get("device_id")
            channels = fwd_pair_info.get("channels")
            if device_id is not None and isinstance(channels, list) and len(channels) == 2:
                ch1, ch2 = channels
                self.fwd_chan_lookup[(device_id, ch1)] = ch2
                self.fwd_chan_lookup[(device_id, ch2)] = ch1
            else:
                log_info(f"Warning: Invalid or incomplete 'forwarding_channel_pairs' entry: {fwd_pair_info}. Skipping.")

        # Build EDM map from "inter_device_channel_pairs"
        for conn in topology.get("inter_device_channel_pairs", []):
            local_dev_id = conn.get("local_device_id")
            local_eth_ch = conn.get("local_eth_channel")
            remote_dev_id = conn.get("remote_device_id")
            remote_eth_ch = conn.get("remote_eth_channel")

            if not all(val is not None for val in [local_dev_id, local_eth_ch, remote_dev_id, remote_eth_ch]):
                log_info(f"Warning: Missing required keys in 'inter_device_channel_pairs' entry: {conn}. Skipping.")
                continue

            self.edm_map[(local_dev_id, local_eth_ch)] = EDMInfo(
                local_dev_id, local_eth_ch, remote_dev_id, remote_eth_ch
            )
            # Create symmetric entries in edm_map as per original logic to support bidirectional lookups
            self.edm_map[(remote_dev_id, remote_eth_ch)] = EDMInfo(
                remote_dev_id, remote_eth_ch, local_dev_id, local_eth_ch
            )

        # eth_chan_to_coord processing
        raw_eth_chan_to_coord = topology.get("eth_chan_to_coord", {})
        if isinstance(raw_eth_chan_to_coord, dict):
            for k, v in raw_eth_chan_to_coord.items():
                try:
                    key = int(k)
                    if isinstance(v, list) and len(v) == 2:
                        self.eth_chan_to_coord[key] = tuple(v)
                    else:
                        log_info(f"Warning: Invalid value format for key '{k}' in 'eth_chan_to_coord': {v}. Skipping.")
                except ValueError:
                    log_info(f"Warning: Invalid key format '{k}' in 'eth_chan_to_coord'. Must be integer. Skipping.")
        else:
            log_info("Warning: 'eth_chan_to_coord' is not a dictionary or not found. Using empty map.")
            # self.eth_chan_to_coord is already initialized as {}

        # links_to_neighbors processing remains the same, derived from the new edm_map
        self.links_to_neighbors = defaultdict(list)
        for edm_info in self.edm_map.values():
            self.links_to_neighbors[edm_info.local_device_id].append(edm_info)

    def select_optimal_channel_for_route(
        self, src_device: int, dst_device: int
    ) -> (int, int):
        """Selects the optimal channel and hop count for a route between two
        devices in the topology graph.  Returns the optimal channel and the
        number of hops"""
        assert src_device != dst_device
        possible_paths = []
        for start_chan in self.links_to_neighbors[src_device]:
            hops = 0
            edm_info = start_chan
            while hops < 100:  # prevent infinite loop
                current_chan = edm_info.paired_remote_chan
                current_device = edm_info.paired_remote_device_id
                hops += 1
                if current_device == dst_device:
                    break
                # follow forwarding path

                if not (current_device, current_chan) in self.fwd_chan_lookup:
                    break
                fwd_chan = self.fwd_chan_lookup[(current_device, current_chan)]

                if (current_device, fwd_chan) not in self.edm_map:
                    break
                edm_info = self.edm_map[(current_device, fwd_chan)]

            if current_device == dst_device:
                possible_paths.append((hops, start_chan.local_eth_chan))

        # find shortest path in possible paths
        assert len(possible_paths) > 0

        shortest_path = min(possible_paths, key=lambda x: x[0])
        return shortest_path

    def find_optimal_path(
        self,
        src_coord: Tuple[int, int],
        dst_coord: Tuple[int, int],
        src_device: int,
        dst_device: int,
    ) -> List[Dict]:
        hops, start_chan = self.select_optimal_channel_for_route(src_device, dst_device)
        path, _ = self.find_path_and_destination(
            src_coord, dst_coord, src_device, start_chan, hops
        )
        return path

    def find_path_and_destination(
        self,
        src_coord: Tuple[int, int],
        dst_coord: Tuple[int, int],
        src_device: int,
        send_chan: int,
        hops: int,
        first_route_noc_type: str = "NOC_0",
    ) -> List[Dict]:
        """Find path by following eth_channel for given hops
        Returns list of dicts with device_id, send_channel, recv_channel for each hop"""

        DEFAULT_FABRIC_NOC_TYPE="NOC_0"

        # infer first route segment from src worker to local eth router
        first_router_coord = self.eth_chan_to_coord[send_chan]
        path = [
            {
                "device": src_device,
                "noc": first_route_noc_type,
                "segment_start_x": src_coord[0],
                "segment_start_y": src_coord[1],
                "segment_end_x": first_router_coord[0],
                "segment_end_y": first_router_coord[1],
            }
        ]
        curr_dev = src_device
        remaining_hops = hops

        curr_chan = send_chan
        while remaining_hops > 0:
            edm_info = self.edm_map.get((curr_dev, curr_chan))
            if edm_info:
                curr_dev = edm_info.paired_remote_device_id
                curr_chan = edm_info.paired_remote_chan
                # print(f"  REMOTE  CONN DEV{curr_dev}, CHAN{curr_chan}")

                if remaining_hops > 1:
                    # add forwarding connection
                    fwd_chan = self.fwd_chan_lookup.get((curr_dev, curr_chan))
                    if fwd_chan is None:
                        log_error(
                            f"No forwarding channel found for DEV{curr_dev}, CHAN{curr_chan}"
                        )
                        return None

                    start_coord = self.eth_chan_to_coord[curr_chan]
                    end_coord = self.eth_chan_to_coord[fwd_chan]
                    path.append(
                        {
                            "device": curr_dev,
                            "noc": DEFAULT_FABRIC_NOC_TYPE,
                            "segment_start_x": start_coord[0],
                            "segment_start_y": start_coord[1],
                            "segment_end_x": end_coord[0],
                            "segment_end_y": end_coord[1],
                        }
                    )
                    curr_chan = fwd_chan
                    # print(f"  FORWARD CONN DEV{curr_dev}, CHAN{curr_chan}")

            else:  # No connection found for hop
                log_error(f"No connection found for DEV{curr_dev}, CHAN{curr_chan}")
                return None
            remaining_hops -= 1

        dst_device_id = curr_dev

        # infer last route segment from src worker to local eth router
        last_router_coord = self.eth_chan_to_coord[curr_chan]
        path.append(
            {
                "device": curr_dev,
                "noc": DEFAULT_FABRIC_NOC_TYPE,
                "segment_start_x": last_router_coord[0],
                "segment_start_y": last_router_coord[1],
                "segment_end_x": dst_coord[0],
                "segment_end_y": dst_coord[1],
            }
        )

        return path, dst_device_id


def log_error(message):
    RED = "\033[91m"
    BOLD = "\033[1m"
    RESET = "\033[0m"
    print(f"{RED}{BOLD}E: {message}{RESET}", file=sys.stderr)


def log_info(message):
    BOLD = "\033[1m"
    RESET = "\033[0m"
    leading_space = len(message) - len(message.lstrip())
    stripped_message = message.strip()
    print(" " * leading_space + f"I: {stripped_message}{RESET}")


def process_traces(
    topology_file: str,
    trace_files: List[str],
    output_file: str,
    remove_desynchronized_events: bool,
):
    """Process specified trace files and combine them with elaborated fabric paths"""
    # Load topology
    topology = TopologyGraph(topology_file)

    # Read and combine all events
    all_events = []
    for trace_file in trace_files:
        file_size = os.path.getsize(trace_file)
        with open(trace_file, "rb") as f:
            events = orjson.loads(f.read())
            log_info(
                f"Reading trace file '{trace_file}' ({len(events)} noc trace events) ... "
            )
            all_events.extend(events)

    # Sort events by timestamp
    all_events.sort(key=lambda x: x["timestamp"])

    if remove_desynchronized_events:
        log_info("Applying --remove_desynchronized_events filter...")
        original_event_count = len(all_events)
        if original_event_count > 1:  # Need at least two events to compare
            cleaned_events = []
            cleaned_events.append(all_events[0])  # Always keep the first event
            for i in range(1, original_event_count):
                prev_event_ts = all_events[i - 1]["timestamp"]
                current_event_ts = all_events[i]["timestamp"]
                if current_event_ts > (2 * prev_event_ts) and prev_event_ts > 1000:
                    log_info(
                        f"desynchronized event detected starting at cycle {prev_event_ts}. Previous timestamp: {prev_event_ts}, current: {current_event_ts}. Dropping this and subsequent events."
                    )
                    break  # Drop current and all remaining events
                cleaned_events.append(all_events[i])
            if len(cleaned_events) < original_event_count:
                log_info(
                    f"Removed {original_event_count - len(cleaned_events)} desynchronized events. Current event count: {len(cleaned_events)}"
                )
            all_events = cleaned_events

    # Elaborate fabric paths
    for event in all_events:
        if "fabric_send" in event:
            src_coord = (event["sx"], event["sy"])
            dst_coord = (event["dx"], event["dy"])
            src_dev = event["src_device_id"]
            eth_chan = event["fabric_send"]["eth_chan"]
            hops = event["fabric_send"]["hops"]
            # First route to the first eth router may be NOC_1 or NOC_0.
            # All subsequent fabric routes use NOC_0
            first_route_noc_type = event["noc"]

            # Find complete path with send/receive channels
            path, dst_device_id = topology.find_path_and_destination(
                src_coord, dst_coord, src_dev, eth_chan, hops, first_route_noc_type
            )
            if path is None:
                log_error(f"No path found for DEV{src_dev}, CHAN{eth_chan}, HOPS{hops}")
                continue

            # save elaborated path to event
            event["fabric_send"]["path"] = path
            # overwrite dst_device_id with true destination device
            event["dst_device_id"] = dst_device_id

    # Write combined and elaborated events
    log_info(f"Writing combined trace to '{output_file}'")
    with open(output_file, "wb") as f:
        f.write(orjson.dumps(all_events, option=orjson.OPT_INDENT_2))


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Process NOC trace files",
        usage="%(prog)s topology_file output_file -- trace_file [trace_file ...]",
    )
    parser.add_argument("topology_file", help="Path to topology.json file")
    parser.add_argument("output_file", help="Output file for combined traces")
    parser.add_argument("trace_files", nargs="*", help="Trace files to process")

    # Split args at -- to handle trace files separately
    try:
        split_idx = sys.argv.index("--")
        main_args = sys.argv[1:split_idx]
        trace_files = sys.argv[split_idx + 1 :]
        if not trace_files:
            parser.error("No trace files specified after --")
    except ValueError:
        parser.error("Please specify trace files after --")

    args = parser.parse_args(main_args)
    remove_desynchronized_events = True
    process_traces(
        args.topology_file, trace_files, args.output_file, remove_desynchronized_events
    )


if __name__ == "__main__":
    main()

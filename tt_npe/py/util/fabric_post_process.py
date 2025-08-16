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
from enum import Enum


def strip_comments(json_str: str) -> str:
    """Strip JavaScript-style comments from JSON string"""
    # First remove single-line comments (//...)
    json_str = re.sub(r"//.*$", "", json_str, flags=re.MULTILINE)
    # Then remove multi-line comments (/* ... */)
    json_str = re.sub(r"/\*.*?\*/", "", json_str, flags=re.DOTALL)
    return json_str

class ProcessingError(Exception):
    def __init__(self, msg: str):
        self.msg = msg
        super().__init__(self.msg)
    
    def __str__(self):
        return self.msg

class RoutingDirection(Enum):
    N = 0 
    S = 1 
    E = 2
    W = 3

    def get_inverse_direction(self):
        if (self == RoutingDirection.N):
            return RoutingDirection.S
        elif (self == RoutingDirection.S):
            return RoutingDirection.N
        elif (self == RoutingDirection.E):
            return RoutingDirection.W
        elif (self == RoutingDirection.W):
            return RoutingDirection.E

class TopologyGraph:
    def __init__(self, topology_file: str):
        """Initialize topology graph from topology.json file"""
        with open(topology_file, "r") as f:
            # Strip comments before parsing JSON
            json_str = strip_comments(f.read())
            topology = orjson.loads(json_str)

        # mapping from (device_id, routing_plane_id, direction) to ethernet_channel
        self.mesh_shapes: Dict[int, Tuple[int, int]] = {}
        self.fabric_config: str = topology.get("fabric_config", "UNKNOWN")
        self.routing_planes: Dict[(int, int, RoutingDirection), int] = {}
        self.device_id_to_fabric_node_id: Dict[int, Tuple[int, int]] = {}
        self.fabric_node_id_to_device_id: Dict[Tuple[int, int], int] = {}
        self.eth_chan_to_coord: Dict[int, Tuple[int, int]] = {}

        # device_id_to_fabric_node_id processing
        for mesh_id_and_shape in topology.get("mesh_shapes", []):
            mesh_id = mesh_id_and_shape.get("mesh_id")
            mesh_shape = mesh_id_and_shape.get("shape")
            if isinstance(mesh_shape, list) and len(mesh_shape) == 2:
                self.mesh_shapes[mesh_id] = tuple(mesh_shape)
            else:
                raise ProcessingError(f"Mesh shape has invalid format: {mesh_shape}")
        
        if (self.fabric_config != "FABRIC_1D" and self.fabric_config != "FABRIC_1D_RING" and self.fabric_config != "FABRIC_2D" and self.fabric_config != "FABRIC_2D_TORUS"):            
            raise ProcessingError(f"Unsupported fabric config: {self.fabric_config}.")

        if len(self.mesh_shapes) > 1:
            raise ProcessingError(f"Multiples meshes are not currently supported.")

        # Build routing_planes map
        for device_routing_planes in topology.get("routing_planes", []):
            device_id = device_routing_planes.get("device_id")

            if device_id is None:
                raise ProcessingError(f"Missing device_id in routing_planes entry: {device_routing_planes}.")

            for device_routing_plane in device_routing_planes.get("device_routing_planes", []):
                routing_plane_id = device_routing_plane.get("routing_plane_id")

                if routing_plane_id is None:
                    raise ProcessingError(f"Missing routing_plane_id in device_routing_planes entry: {device_routing_plane}.")

                for direction in RoutingDirection:
                    eth_chan = device_routing_plane.get("ethernet_channels").get(direction.name)
                    if eth_chan is not None:
                        self.routing_planes[(device_id, routing_plane_id, direction)] = eth_chan

        # device_id_to_fabric_node_id processing
        raw_device_id_to_fabric_node_id = topology.get("device_id_to_fabric_node_id", {})
        if isinstance(raw_device_id_to_fabric_node_id, dict):
            for device_id_str, fabric_node_id in raw_device_id_to_fabric_node_id.items():
                try:
                    device_id = int(device_id_str)
                    if isinstance(fabric_node_id, list) and len(fabric_node_id) == 2:
                        self.device_id_to_fabric_node_id[device_id] = tuple(fabric_node_id)
                        self.fabric_node_id_to_device_id[tuple(fabric_node_id)] = device_id
                    else:
                        raise ProcessingError(f"Invalid value format for key '{device_id_str}' in 'device_id_to_fabric_node_id': {fabric_node_id}.")
                except ValueError:
                    raise ProcessingError(f"Invalid key format '{device_id_str}' in 'device_id_to_fabric_node_id'. Must be integer.")
        else:
            raise ProcessingError("'device_id_to_fabric_node_id' is not a dictionary or not found.")
            # self.device_id_to_fabric_node_id is already initialized as {}
        
        # eth_chan_to_coord processing
        raw_eth_chan_to_coord = topology.get("eth_chan_to_coord", {})
        if isinstance(raw_eth_chan_to_coord, dict):
            for k, v in raw_eth_chan_to_coord.items():
                try:
                    key = int(k)
                    if isinstance(v, list) and len(v) == 2:
                        self.eth_chan_to_coord[key] = tuple(v)
                    else:
                        raise ProcessingError(f"Invalid value format for key '{k}' in 'eth_chan_to_coord': {v}.")
                except ValueError:
                    raise ProcessingError(f"Invalid key format '{k}' in 'eth_chan_to_coord'. Must be integer.")
        else:
            raise ProcessingError("'eth_chan_to_coord' is not a dictionary or not found. Using empty map.")
            # self.eth_chan_to_coord is already initialized as {}

        # links_to_neighbors processing remains the same, derived from the new edm_map
        self.links_to_neighbors = defaultdict(list)

    # TO DO: remove?
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
        path, _ = self.find_path_and_destination_1d(
            src_coord, dst_coord, src_device, start_chan, hops
        )
        return path
    
    def get_next_device_in_dir(self, device_id, direction):
        fabric_node_id = self.device_id_to_fabric_node_id[device_id]
        mesh_id = fabric_node_id[0]
        ew_dim = self.mesh_shapes[mesh_id][1]

        if direction == RoutingDirection.N:
            next_device = (fabric_node_id[0], fabric_node_id[1] - ew_dim)
        elif direction == RoutingDirection.S:
            next_device = (fabric_node_id[0], fabric_node_id[1] + ew_dim)
        elif direction == RoutingDirection.E:
            next_device = (fabric_node_id[0], fabric_node_id[1] + 1)
        elif direction == RoutingDirection.W:
            next_device = (fabric_node_id[0], fabric_node_id[1] - 1)
        else:
            raise ProcessingError(f"Invalid direction: {direction}")
        
        # check if next_device is a valid fabric node
        if next_device[1] < 0 or next_device[1] >= self.mesh_shapes[mesh_id][0] * self.mesh_shapes[mesh_id][1]:
            raise ProcessingError(
                f"there is no device in direction {direction} of FABRIC NODE ID={fabric_node_id}"
            )
        
        return self.fabric_node_id_to_device_id[next_device]
    
    def add_first_hop(
        self,
        path_to_update,
        src_coord: Tuple[int, int],
        src_device: int,
        send_chan: int,
        fabric_mux: Dict | None,
        first_route_noc_type: str = "NOC_0",
    ) -> List[Dict]:
        """Find path by following eth_channel for given hops
        Returns list of dicts with device_id, send_channel, recv_channel for each hop"""

        # infer first route segment from src worker to local eth router
        first_router_coord = self.eth_chan_to_coord[send_chan]

        if fabric_mux is not None:
            path_to_update.append(
                {
                    "device": src_device,
                    "noc": first_route_noc_type,
                    "segment_start_x": src_coord[0],
                    "segment_start_y": src_coord[1],
                    "forward_x": fabric_mux["x"],
                    "forward_y": fabric_mux["y"],
                    "parent_id": -1,
                }
            )
            path_to_update.append(
                {
                    "device": src_device,
                    "noc": fabric_mux["noc"],
                    "segment_start_x": fabric_mux["x"],
                    "segment_start_y": fabric_mux["y"],
                    "forward_x": first_router_coord[0],
                    "forward_y": first_router_coord[1],
                    "parent_id": 0,
                }
            )
        else:
            path_to_update.append(
                {
                    "device": src_device,
                    "noc": first_route_noc_type,
                    "segment_start_x": src_coord[0],
                    "segment_start_y": src_coord[1],
                    "forward_x": first_router_coord[0],
                    "forward_y": first_router_coord[1],
                    "parent_id": -1,
                }
            )

    def add_hops(self, path_to_update, src_dev, src_coord, start_distance, range_devices, direction, routing_plane_id, 
        dst_coords, terminate, parent_id_start):
        DEFAULT_FABRIC_NOC_TYPE="NOC_1"
        total_hops = start_distance + range_devices - 1
        curr_dev = src_dev
        receiver_coord = src_coord

        for hop in range(1, total_hops + 1):            
            # log_info(f"  REMOTE  CONN DEV{curr_dev}, CHAN{curr_chan}")
            path_segment = {
                    "device": curr_dev,
                    "noc": DEFAULT_FABRIC_NOC_TYPE,
                    "parent_id": parent_id_start,
                    "segment_start_x": receiver_coord[0],
                    "segment_start_y": receiver_coord[1],
                }

            # add forwarding connection
            if hop < total_hops or not terminate:
                fwd_chan = self.routing_planes.get((curr_dev, routing_plane_id, direction))

                if fwd_chan is None:
                    raise ProcessingError(f"No ethernet channel on device {curr_dev} in dir {direction} " \
                        f"on routing plane {routing_plane_id}")

                fwd_coord = self.eth_chan_to_coord[fwd_chan]
                path_segment.update(
                    {
                        "forward_x": fwd_coord[0],
                        "forward_y": fwd_coord[1],
                    }
                )
                # log_info(f"  FORWARD CONN DEV{curr_dev}, CHAN{curr_chan}")
            
            # add local write
            if hop >= start_distance and terminate:
                path_segment.update({"local_writes": dst_coords})
                
            path_to_update.append(path_segment)
            parent_id_start += 1

            # get next receivers device id and ethernet core coord
            if hop < total_hops or not terminate:
                curr_dev = self.get_next_device_in_dir(curr_dev, direction)
                receiver_channel = self.routing_planes.get((curr_dev, routing_plane_id, direction.get_inverse_direction()))

                if receiver_channel is None: 
                    raise ProcessingError(f"No ethernet channel on device {curr_dev} in dir {direction.get_inverse_direction()}" \
                        f"on routing plane {routing_plane_id}")
                
                receiver_coord = self.eth_chan_to_coord[receiver_channel]
        
        return curr_dev, receiver_coord

    def find_path_and_destination_1d(
        self,
        src_coord,
        dst_coords: List[Dict],
        src_device: int,
        eth_chan,
        start_distance: int,
        range_devices: int,
        fabric_mux,
        first_route_noc_type
    ) -> List[Dict]:
        """Find path by following eth_channel for given hops
        Returns list of dicts with device_id, send_channel, recv_channel for each hop"""

        path = []
        self.add_first_hop(path, src_coord, src_device, eth_chan, fabric_mux, first_route_noc_type)
        last_parent_id = 0 if fabric_mux is None else 1
        
        # find routing plane
        path_routing_plane_id = None
        initial_direction = None
        for (device_id, routing_plane_id, direction), eth_chan_ in self.routing_planes.items():
            if device_id == src_device and eth_chan_ == eth_chan:
                path_routing_plane_id = routing_plane_id
                initial_direction = direction

        if path_routing_plane_id is None or initial_direction is None:
            raise ProcessingError(
                f"Unable to find associated routing plane and direction for DEV{src_device}, SEND_CHAN{send_chan}"
            )

        curr_dev = self.get_next_device_in_dir(src_device, initial_direction)
        receiver_channel = self.routing_planes.get((curr_dev, routing_plane_id, direction.get_inverse_direction()))
        if receiver_channel is None: 
            raise ProcessingError(f"No ethernet channel on device {curr_dev} in dir {direction.get_inverse_direction()}" \
                f"on routing plane {routing_plane_id}")
        
        receiver_coord = self.eth_chan_to_coord[receiver_channel]

        curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, start_distance, range_devices, initial_direction, path_routing_plane_id, dst_coords, True, last_parent_id)

        return path, curr_dev

    def find_path_and_destination_2d(
        self,
        src_coord,
        dst_coords: List[Dict],
        src_device: int,
        eth_chan,
        ns_hops: int,
        e_hops: int,
        w_hops: int,
        is_mcast: bool,
        fabric_mux,
        first_route_noc_type
    ) -> List[Dict]:
        """Find path by following eth_channel for given hops
        Returns list of dicts with device_id, send_channel, recv_channel for each hop"""

        path = []
        self.add_first_hop(path, src_coord, src_device, eth_chan, fabric_mux, first_route_noc_type)
        last_parent_id = 0 if fabric_mux is None else 1
        
        # find routing plane
        path_routing_plane_id = None
        initial_direction = None
        for (device_id, routing_plane_id, direction), eth_chan_ in self.routing_planes.items():
            if device_id == src_device and eth_chan_ == eth_chan:
                path_routing_plane_id = routing_plane_id
                initial_direction = direction

        if path_routing_plane_id is None or initial_direction is None:
            raise ProcessingError(
                f"Unable to find associated routing plane and direction for DEV{src_device}, SEND_CHAN{send_chan}"
            )

        curr_dev = self.get_next_device_in_dir(src_device, initial_direction)
        receiver_channel = self.routing_planes.get((curr_dev, routing_plane_id, direction.get_inverse_direction()))
        if receiver_channel is None: 
            raise ProcessingError(f"No ethernet channel on device {curr_dev} in dir {direction.get_inverse_direction()}" \
                f"on routing plane {routing_plane_id}")
        
        receiver_coord = self.eth_chan_to_coord[receiver_channel]
        
        if is_mcast:
            if ns_hops == 0 and e_hops == 0 and w_hops == 0: 
                raise ProcessingError(f"ns_hops, e_hops, and w_hops are all 0")
            
            if ns_hops != 0: 
                # add mcast trunk then e/w branches (with correct parent_id_start)
                curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, 1, ns_hops, initial_direction, path_routing_plane_id, dst_coords, True, last_parent_id)
                for i in range(1, ns_hops + 1):
                    last_parent_id += 1
                    if e_hops != 0:
                        curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, 1, e_hops, RoutingDirection.E, path_routing_plane_id, dst_coords, True, last_parent_id)
                    if w_hops != 0:
                        curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, 1, w_hops, RoutingDirection.W, path_routing_plane_id, dst_coords, True, last_parent_id)
            else:
                # E/W line mcast
                if e_hops != 0 and w_hops != 0: 
                    raise ProcessingError(f"Both e_hops and w_hops can't be non-zero in E/W line mcast (e_hops={e_hops}, w_hops={w_hops})")

                if e_hops != 0:
                    curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, 1, e_hops, RoutingDirection.E, path_routing_plane_id, dst_coords, True, last_parent_id)
                else:
                    curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, 1, w_hops, RoutingDirection.W, path_routing_plane_id, dst_coords, True, last_parent_id)
        else:
            # Unicast Edge case: if initial_direction is N/S, then n/s hops will be 1 more and and e/w hops will be 1 less than the "correct" values
            # This is a side effect of having to read the route buffer to deduce the number of hops
            # (the device on which the "turn" happens is an e/w hop on the route buffer instead of n/s)
            # However, we don't need to correct this since it's correctly accounted for when creating the path            
            if ns_hops == 0 and e_hops == 0 and w_hops == 0: 
                raise ProcessingError(f"ns_hops, e_hops, and w_hops are all 0")
            if e_hops != 0 and w_hops != 0: 
                raise ProcessingError(f"Both e_hops and w_hops can't be non-zero in unicast (e_hops={e_hops}, w_hops={w_hops})")

            if initial_direction == RoutingDirection.E or initial_direction == RoutingDirection.W:
                curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, e_hops if initial_direction == RoutingDirection.E else w_hops, 1, 
                    initial_direction, path_routing_plane_id, dst_coords, True, last_parent_id)
            else:
                # dimension order routing: traverse N/S hops first, then E/W hops
                curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, ns_hops, 1, initial_direction, path_routing_plane_id, dst_coords, e_hops == 0 and w_hops == 0, last_parent_id)
                last_parent_id += ns_hops
                
                if e_hops != 0:
                    curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, e_hops, 1, RoutingDirection.E, path_routing_plane_id, dst_coords, True, last_parent_id)
                elif w_hops != 0:
                    curr_dev, receiver_coord = self.add_hops(path, curr_dev, receiver_coord, w_hops, 1, RoutingDirection.W, path_routing_plane_id, dst_coords, True, last_parent_id)

        return path, curr_dev


def log_error(message):
    RED = "\033[91m"
    BOLD = "\033[1m"
    RESET = "\033[0m"
    print(f"{RED}{BOLD}E: {message}{RESET}", file=sys.stderr)

def log_warning(message):
    YELLOW = '\033[93m'
    BOLD = "\033[1m"
    RESET = "\033[0m"
    print(f"{YELLOW}{BOLD}W: {message}{RESET}", file=sys.stderr)

def log_info(message, quiet):
    if quiet: return
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
    quiet: bool,
):
    """Process specified trace files and combine them with elaborated fabric paths"""
    try:
        # Load topology
        topology = TopologyGraph(topology_file)

        # Read and combine all events
        all_events = []
        for trace_file in trace_files:
            file_size = os.path.getsize(trace_file)
            with open(trace_file, "rb") as f:
                events = orjson.loads(f.read())
                log_info(
                    f"Reading trace file '{trace_file}' ({len(events)} noc trace events) ... ",
                    quiet
                )
                all_events.extend(events)

        # Sort events by timestamp
        all_events.sort(key=lambda x: x["timestamp"])

        if remove_desynchronized_events:
            log_info("Applying --remove_desynchronized_events filter...", quiet)
            original_event_count = len(all_events)
            if original_event_count > 1:  # Need at least two events to compare
                cleaned_events = []
                cleaned_events.append(all_events[0])  # Always keep the first event
                for i in range(1, original_event_count):
                    prev_event_ts = all_events[i - 1]["timestamp"]
                    current_event_ts = all_events[i]["timestamp"]
                    if current_event_ts > (2 * prev_event_ts) and prev_event_ts > 1000:
                        log_info(
                            f"desynchronized event detected starting at cycle {prev_event_ts}. Previous timestamp: {prev_event_ts}, current: {current_event_ts}. Dropping this and subsequent events.",
                            quiet
                        )
                        break  # Drop current and all remaining events
                    cleaned_events.append(all_events[i])
                if len(cleaned_events) < original_event_count:
                    log_info(
                        f"Removed {original_event_count - len(cleaned_events)} desynchronized events. Current event count: {len(cleaned_events)}",
                        quiet
                    )
                all_events = cleaned_events

        # Elaborate fabric paths
        for event in all_events:
            if "fabric_send" in event:
                src_coord = (event["sx"], event["sy"])
                dst_coords = event["dst"]
                src_dev = event["src_device_id"]
                eth_chan = event["fabric_send"]["eth_chan"]
                fabric_mux = event["fabric_send"].get("fabric_mux")
                # First route to the first eth router may be NOC_1 or NOC_0.
                # All subsequent fabric routes use NOC_0
                first_route_noc_type = event["noc"]

                # Find complete path with send/receive channels
                if topology.fabric_config == "FABRIC_1D" or topology.fabric_config == "FABRIC_1D_RING":
                    start_distance = event["fabric_send"]["start_distance"]
                    range_devices = event["fabric_send"]["range"]
                
                    path, dst_device_id = topology.find_path_and_destination_1d(
                        src_coord, dst_coords, src_dev, eth_chan, start_distance, range_devices, fabric_mux, first_route_noc_type
                    )
                elif topology.fabric_config == "FABRIC_2D" or topology.fabric_config == "FABRIC_2D_TORUS":
                    ns_hops = event["fabric_send"]["ns_hops"]
                    e_hops = event["fabric_send"]["e_hops"]
                    w_hops = event["fabric_send"]["w_hops"]
                    is_mcast = event["fabric_send"]["is_mcast"]
                    path, dst_device_id = topology.find_path_and_destination_2d(
                        src_coord, dst_coords, src_dev, eth_chan, ns_hops, e_hops, w_hops, is_mcast, fabric_mux, first_route_noc_type
                    )
                
                if path is None:
                    raise ProcessingError(f"No path found for DEV{src_dev}, Routing Info: {event['fabric_send']}")
                    continue

                # save elaborated path to event
                event["fabric_send"]["path"] = path
                # overwrite dst_device_id with true destination device
                event["dst_device_id"] = dst_device_id

        # Write combined and elaborated events
        log_info(f"Writing combined trace to '{output_file}'", quiet)
        with open(output_file, "wb") as f:
            f.write(orjson.dumps(all_events, option=orjson.OPT_INDENT_2))
    except ProcessingError as e:
        log_error(f"{e}")

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
        args.topology_file, trace_files, args.output_file, remove_desynchronized_events, False
    )


if __name__ == "__main__":
    main()

// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <boost/unordered/unordered_flat_set.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include "ScopedTimer.hpp"
#include "magic_enum.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModelFactory.hpp"
#include "ingestWorkload.hpp"
#include "npeUtil.hpp"
#include "npeWorkload.hpp"
#include "simdjson.h"

namespace tt_npe {

template <typename T>
T get_with_default(simdjson::simdjson_result<T> element, T default_value) {
    T val;
    if (std::move(element).get(val) != simdjson::SUCCESS) {
        return default_value;
    }
    return val;
}

std::optional<npeWorkload> loadJSONWorkloadFormat(const std::string &wl_filename, bool verbose) {
    ScopedTimer st("", true);
    npeWorkload wl;

    if (not std::filesystem::exists(wl_filename)) {
        log_error("Provided workload file '{}' is not a valid file!", wl_filename);
        return {};
    } else if (not wl_filename.ends_with(".json")) {
        if (not promptUser("Provided workload file does not have .json file extension; are you "
                           "sure you want to load this?")) {
            return {};
        }
    }

    try {
        // load config file
        simdjson::dom::parser parser;
        simdjson::dom::element json_data;
        try {
            // use simdjson library to parse JSON file
            json_data = parser.load(wl_filename);
        } catch (const simdjson::simdjson_error &exp) {
            log_error(
                "Encountered parsing error while reading JSON workload file '{}': {}",
                wl_filename,
                exp.what());
            return {};
        }

        if (json_data.type() != simdjson::dom::element_type::OBJECT) {
            log_error(
                "JSON workload file '{}' is not structured as an object (map) at the top level!",
                wl_filename);
            log_error(
                "Its likely you are trying to load a tt-metal noc trace file; for tt_npe_run use "
                "-t to specify a trace file");
            return {};
        }

        boost::unordered_flat_map<DeviceID, std::pair<CycleCount, CycleCount>> golden_cycles;
        if (json_data["golden_result"].error() == simdjson::SUCCESS &&
            json_data["golden_result"]["cycles"].error() == simdjson::SUCCESS) {
            // retrieve golden cycles if it exists
            golden_cycles[0] = {0, json_data["golden_result"]["cycles"].get_uint64().value()};
            wl.setGoldenResultCycles(golden_cycles);
        }

        simdjson::dom::array phases;
        if (json_data["phases"].get_array().error() != simdjson::SUCCESS) {
            log_error("No workload phases declared within workload file '{}'!", wl_filename);
            return {};
        }
        phases = json_data["phases"].get_array();

        DeviceID device_id = 0;
        for (const auto &phase : phases) {
            npeWorkloadPhase ph;
            simdjson::dom::array transfers = phase["transfers"].get_array();
            simdjson::error_code err;
            for (const auto &transfer : transfers) {
                int64_t packet_size, num_packets, src_x, src_y, src_device_id;
                if (transfer["packet_size"].get_int64().get(packet_size) != simdjson::SUCCESS) {
                    log_error(
                        "Transfer event missing 'packet_size' in workload file '{}'", wl_filename);
                    continue;
                }
                if (transfer["num_packets"].get_int64().get(num_packets) != simdjson::SUCCESS) {
                    log_error(
                        "Transfer event missing 'num_packets' in workload file '{}'", wl_filename);
                    continue;
                }
                if (transfer["src_x"].get_int64().get(src_x) != simdjson::SUCCESS) {
                    log_error("Transfer event missing 'src_x' in workload file '{}'", wl_filename);
                    continue;
                }
                if (transfer["src_y"].get_int64().get(src_y) != simdjson::SUCCESS) {
                    log_error("Transfer event missing 'src_y' in workload file '{}'", wl_filename);
                    continue;
                }
                if (transfer["device_id"].get_int64().get(src_device_id) != simdjson::SUCCESS) {
                    src_device_id = 0;
                }

                // determine if multicast or unicast based on presence of dst_x and dst_y
                NocDestination noc_dest;
                int64_t dst_x, dst_y, dst_device_id;
                if (transfer["dst_x"].get_int64().get(dst_x) != simdjson::SUCCESS) {
                    dst_x = -1;
                }
                if (transfer["dst_y"].get_int64().get(dst_y) != simdjson::SUCCESS) {
                    dst_y = -1;
                }
                if (transfer["device_id"].get_int64().get(dst_device_id) != simdjson::SUCCESS) {
                    dst_device_id = 0;
                }
                if (dst_x == -1 && dst_y == -1) {
                    int64_t mcast_start_x, mcast_start_y, mcast_end_x, mcast_end_y;
                    if (transfer["mcast_start_x"].get_int64().get(mcast_start_x) !=
                        simdjson::SUCCESS) {
                        log_error(
                            "Multicast Transfer event missing 'mcast_start_x' in workload file "
                            "'{}'; skipping ... ",
                            wl_filename);
                        continue;
                    }
                    if (transfer["mcast_start_y"].get_int64().get(mcast_start_y) !=
                        simdjson::SUCCESS) {
                        log_error(
                            "Multicast Transfer event missing 'mcast_start_y' in workload file "
                            "'{}'; skipping ... ",
                            wl_filename);
                        continue;
                    }
                    if (transfer["mcast_end_x"].get_int64().get(mcast_end_x) != simdjson::SUCCESS) {
                        log_error(
                            "Multicast Transfer event missing 'mcast_end_x' in workload file '{}'; "
                            "skipping ... ",
                            wl_filename);
                        continue;
                    }
                    if (transfer["mcast_end_y"].get_int64().get(mcast_end_y) != simdjson::SUCCESS) {
                        log_error(
                            "Multicast Transfer event missing 'mcast_end_y' in workload file '{}'; "
                            "skipping ... ",
                            wl_filename);
                        continue;
                    }

                    noc_dest = MulticastCoordSet(
                        Coord{src_device_id, mcast_start_y, mcast_start_x},
                        Coord{dst_device_id, mcast_end_y, mcast_end_x});
                } else {
                    noc_dest = Coord{dst_device_id, dst_y, dst_x};
                }

                double injection_rate = 0.0;
                if (transfer["injection_rate"].get_double().get(injection_rate) !=
                    simdjson::SUCCESS) {
                    injection_rate = 0.0;
                }
                int64_t phase_cycle_offset = 0;
                if (transfer["phase_cycle_offset"].get_int64().get(phase_cycle_offset) !=
                    simdjson::SUCCESS) {
                    log_warn(
                        "Transfer event missing 'phase_cycle_offset' in workload file '{}'",
                        wl_filename);
                    phase_cycle_offset = 0;
                }
                std::string_view noc_type;
                if (transfer["noc_type"].get_string().get(noc_type) != simdjson::SUCCESS) {
                    log_error(
                        "Transfer event missing 'noc_type' in workload file '{}'", wl_filename);
                    continue;
                }
                std::string_view noc_event_type;
                if (transfer["noc_event_type"].get_string().get(noc_event_type) !=
                    simdjson::SUCCESS) {
                    log_warn(
                        "Transfer event missing 'noc_event_type' in workload file '{}'",
                        wl_filename);
                }

                ph.transfers.emplace_back(
                    packet_size,
                    num_packets,
                    // note: row is y position, col is x position!
                    Coord{src_device_id, src_y, src_x},
                    noc_dest,
                    injection_rate,
                    phase_cycle_offset,
                    (noc_type == "NOC_0") ? nocType::NOC0 : nocType::NOC1,
                    noc_event_type);
            }
            wl.addPhase(ph);
        }
    } catch (const simdjson::simdjson_error &exp) {
        log_error("{}", exp.what());
        return {};
    } catch (const std::exception &exp) {
        log_error("{}", exp.what());
        return {};
    }

    wl.setSourceFilePath(wl_filename);

    if (verbose)
        fmt::println("Workload loaded in {} ms", st.getElapsedTimeMilliSeconds());
    return wl;
}

auto computeGoldenCyclesAndT0(const simdjson::dom::element& event_data_json, std::unique_ptr<npeDeviceModel>& device_model) {
    double t0_timestamp = 2e30;
    boost::unordered_flat_map<
        std::tuple<std::string_view, int64_t, int64_t, int64_t>,
        std::pair<double, double>>
        per_core_ts;

    for (const auto &event : event_data_json.get_array()) {
        double ts = get_with_default(event["timestamp"].get_int64(), int64_t(0));
        t0_timestamp = std::min(t0_timestamp, ts);

        std::string_view proc = get_with_default(event["proc"].get_string(), std::string_view{});
        int64_t sx = get_with_default(event["sx"].get_int64(), int64_t(-1));
        int64_t sy = get_with_default(event["sy"].get_int64(), int64_t(-1));
        int64_t device_id = get_with_default(event["src_device_id"].get_int64(), int64_t(-1));
        if (proc != "" && sx != -1 && sy != -1) {
            auto key = std::make_tuple(proc, sx, sy, device_id);
            auto it = per_core_ts.find(key);
            if (it == per_core_ts.end()) {
                per_core_ts[key] = {ts, ts};
            } else {
                auto &minmax_ts = it->second;
                minmax_ts.first = std::min(minmax_ts.first, ts);
                minmax_ts.second = std::max(minmax_ts.second, ts);
            }
        }
    }

    boost::unordered_flat_set<DeviceID> device_ids_for_stats = device_model->getDeviceIDs();
    boost::unordered_flat_map<DeviceID, std::pair<CycleCount, CycleCount>> golden_cycles;
    for (auto device_id: device_ids_for_stats) {
        size_t min_kernel_cycles = INT64_MAX;
        size_t max_kernel_cycles = 0;
        for (const auto &[key, min_max_ts] : per_core_ts) {
            if (std::get<3>(key) == device_id) {
                min_kernel_cycles = std::min(min_kernel_cycles, (size_t)min_max_ts.first);
                max_kernel_cycles = std::max(max_kernel_cycles, (size_t)min_max_ts.second);
            }
        }
        
        // make min_kernel_cycles and max_kernel_cycles relative to t0
        min_kernel_cycles -= t0_timestamp;
        max_kernel_cycles -= t0_timestamp;
        // there is at least a ~20 cycle overhead between last noc event and kernel end timestamp
        golden_cycles[device_id] = {min_kernel_cycles, max_kernel_cycles - 20};
    }
    
    return std::pair(golden_cycles, t0_timestamp);
}

boost::unordered_flat_map<std::pair<Coord, RiscType>, std::vector<npeZone>> extractZones(const simdjson::dom::element& event_data_json, double t0_timestamp) {
    boost::unordered_flat_map<std::pair<Coord, RiscType>, std::vector<npeZone>> zones;
    for (const auto &event : event_data_json.get_array()) {
        double ts = get_with_default(event["timestamp"].get_int64(), int64_t(0));
        int64_t device_id = get_with_default(event["src_device_id"].get_int64(), int64_t(0));
        std::string_view proc = get_with_default(event["proc"].get_string(), std::string_view{});
        int64_t sx = get_with_default(event["sx"].get_int64(), int64_t(-1));
        int64_t sy = get_with_default(event["sy"].get_int64(), int64_t(-1));
        std::string_view zone = get_with_default(event["zone"].get_string(), std::string_view{});
        std::string_view zone_phase = get_with_default(event["zone_phase"].get_string(), std::string_view{});
        if (proc != "" && sx != -1 && sy != -1 && zone != "" && zone_phase != "") {
            zones[std::pair{Coord(device_id, sy, sx), *magic_enum::enum_cast<RiscType>(proc)}].push_back(
                npeZone(ts - t0_timestamp, std::string(zone), *magic_enum::enum_cast<ZonePhase>(zone_phase))
            );
        }
    }

    return zones;
}

std::string flattenEnclosingZones(const std::vector<std::pair<npeZone, int>>& enclosing_zones) {
    std::string enclosing_zone_path;
    for (int i = 0; i < enclosing_zones.size(); i++) {
        auto& [enclosing_zone, zone_iteration] = enclosing_zones[i];
        if (i != 0) {
            enclosing_zone_path += "/";
        }
        enclosing_zone_path += (enclosing_zone.zone + "[" + std::to_string(zone_iteration) + "]");
    }
    return enclosing_zone_path;
}

std::optional<npeWorkload> convertNocTracesToNpeWorkload(
    const std::string &input_filepath, const std::string &device_name, bool verbose) {
    ScopedTimer st("", true);
    npeWorkload wl;

    auto device_model =
        npeDeviceModelFactory::createDeviceModel(device_name);

    bool is_wormhole_arch = device_model->getArch() == DeviceArch::WormholeB0;
    bool is_blackhole_arch = device_model->getArch() == DeviceArch::Blackhole;

    const boost::unordered_flat_set<std::string_view> SUPPORTED_NOC_EVENTS = {
        "READ",
        "READ_SET_STATE",
        "READ_WITH_STATE",
        "READ_WITH_STATE_AND_TRID",
        "READ_DRAM_SHARDED_SET_STATE",
        "READ_DRAM_SHARDED_WITH_STATE",
        "WRITE_",
        "WRITE_MULTICAST",
        "WRITE_SET_STATE",
        "WRITE_WITH_STATE",
        "FABRIC_UNICAST_WRITE",
        "FABRIC_UNICAST_INLINE_WRITE",
        "FABRIC_UNICAST_ATOMIC_INC",
        "FABRIC_FUSED_UNICAST_ATOMIC_INC",
        "FABRIC_UNICAST_SCATTER_WRITE"
    };

    if (not std::filesystem::exists(input_filepath)) {
        log_error("Provided input file '{}' is not a valid file!", input_filepath);
        return {};
    }

    simdjson::dom::parser parser;
    simdjson::dom::element event_data_json;
    try {
        event_data_json = parser.load(input_filepath);
    } catch (const simdjson::simdjson_error &exp) {
        log_error(
            "Encountered parsing error while reading JSON input file '{}': {}",
            input_filepath,
            exp.what());
        return {};
    }

    auto [golden_cycles, t0_timestamp] = computeGoldenCyclesAndT0(event_data_json, device_model);
    wl.setGoldenResultCycles(golden_cycles);

    boost::unordered_flat_map<std::pair<Coord, RiscType>, std::vector<npeZone>> zones = extractZones(event_data_json, t0_timestamp);
    wl.setZones(std::move(zones));

    struct NoCEventSavedState {
        int64_t sx = 0;
        int64_t sy = 0;
        int64_t dx = 0;
        int64_t dy = 0;
        int64_t num_bytes = 0;
    };

    std::vector<npeWorkloadPhase> phases;
    npeWorkloadPhase phase;
    NoCEventSavedState curr_saved_state_read;
    NoCEventSavedState curr_saved_state_write;
    std::pair<Coord, RiscType> prev_core_proc; 
    std::unique_ptr<ZoneIterator> zone_iterator;
    for (const auto &event : event_data_json.get_array()) {
        std::string_view proc = get_with_default(event["proc"].get_string(), std::string_view{});
        std::string_view noc_event_type =
            get_with_default(event["type"].get_string(), std::string_view{});
        int64_t num_bytes = get_with_default(event["num_bytes"].get_int64(), int64_t(0));
        int64_t sx = get_with_default(event["sx"].get_int64(), int64_t(-1));
        int64_t sy = get_with_default(event["sy"].get_int64(), int64_t(-1));
        int64_t dx = get_with_default(event["dx"].get_int64(), int64_t(-1));
        int64_t dy = get_with_default(event["dy"].get_int64(), int64_t(-1));

        int64_t src_device_id = get_with_default(event["src_device_id"].get_int64(), int64_t(0));
        // ensure that dst_device_id is the same as src_device_id if not specified 
        int64_t dst_device_id = get_with_default(event["dst_device_id"].get_int64(), int64_t(src_device_id));
        double ts = get_with_default(event["timestamp"].get_int64(), int64_t(0));
        
        // initialize (or re-initialize) zone_iterator for current core and 
        // increment until we reach the last zone that is <= (ts - t0_timestamp)
        std::pair<Coord, RiscType> core_proc = {Coord(src_device_id, sy, sx), *magic_enum::enum_cast<RiscType>(proc)};
        try {
            if (wl.getZones().contains(core_proc)) {
                if (core_proc != prev_core_proc) {
                    zone_iterator = make_unique<ZoneIterator>(wl.getZones()[core_proc]);
                    prev_core_proc = core_proc;
                }
                while (!zone_iterator->isEnd() && zone_iterator->getNextZone().timestamp <= ts - t0_timestamp) ++(*zone_iterator);
            }
        } catch (const tt_npe::npeException &exp) {
            throw npeException(npeErrorCode::TRACE_INGEST_FAILED, "Zones are not correctly structured");
        }
        
        // flatten enclosing_zones and store in transfer
        std::string enclosing_zone_path = flattenEnclosingZones(zone_iterator->getEnclosingZones());

        // Filter out unsupported or invalid events
        if (not SUPPORTED_NOC_EVENTS.contains(noc_event_type)) {
            continue;
        }

        if (proc.empty()) {
            log_warn("No processor defined for event; skipping ...");
            continue;
        }

        if ((noc_event_type == "WRITE_" && num_bytes == 0) ||
            (noc_event_type == "READ" && num_bytes == 0)) {
            log_warn("No num_bytes defined for READ/WRITE event; skipping ...");
            continue;
        }

        // Handle events with SET_STATE
        if (noc_event_type.ends_with("SET_STATE")) {
            if (noc_event_type.starts_with("READ")) {
                curr_saved_state_read = {sx, sy, dx, dy, num_bytes};
            } else if (noc_event_type.starts_with("WRITE")) {
                curr_saved_state_write = {sx, sy, dx, dy, num_bytes};
            }
            continue;
        }

        if (noc_event_type.find("WITH_STATE") != std::string::npos) {
            if (noc_event_type.find("READ") != std::string::npos) {
                sx = curr_saved_state_read.sx;
                sy = curr_saved_state_read.sy;
                dx = curr_saved_state_read.dx;
                dy = curr_saved_state_read.dy;
                if (curr_saved_state_read.num_bytes > 0) {
                    num_bytes = curr_saved_state_read.num_bytes;
                }
            } else if (noc_event_type.find("WRITE") != std::string::npos) {
                sx = curr_saved_state_write.sx;
                sy = curr_saved_state_write.sy;
                dx = curr_saved_state_write.dx;
                dy = curr_saved_state_write.dy;
                if (curr_saved_state_write.num_bytes > 0) {
                    num_bytes = curr_saved_state_write.num_bytes;
                }
            }
        }

        // swap src and dst for read events
        if (noc_event_type.starts_with("READ")) {
            std::swap(sx, dx);
            std::swap(sy, dy);
        }

        // Get noc type
        std::string_view noc_type =
            get_with_default(event["noc"].get_string(), std::string_view{""});
        if (noc_type.empty()) {
            log_error("No NoC type specified for event; skipping ...");
            continue;
        }

        int64_t phase_cycle_offset = ts - t0_timestamp;

        // Add latency to phase_cycle_offset (latency for fabric events added later)
        if (noc_event_type.starts_with("READ")) {
            if (is_wormhole_arch) {
                phase_cycle_offset += WormholeB0DeviceModel::get_read_latency(sx, sy, dx, dy);
            } else if (is_blackhole_arch) {
                phase_cycle_offset += BlackholeDeviceModel::get_read_latency(sx, sy, dx, dy);
            } else {
                log_error("Unknown device model: {}", device_name);
                throw npeException(npeErrorCode::TRACE_INGEST_FAILED);
            }
        } else if (noc_event_type.starts_with("WRITE")) {
            if (is_wormhole_arch) {
                phase_cycle_offset +=
                    WormholeB0DeviceModel::get_write_latency(sx, sy, dx, dy, noc_type);
            } else if (is_blackhole_arch) {
                phase_cycle_offset +=
                    BlackholeDeviceModel::get_write_latency(sx, sy, dx, dy, noc_type);
            } else {
                log_error("Unknown device model: {}", device_name);
                throw npeException(npeErrorCode::TRACE_INGEST_FAILED);
            }
        }

        // Compute dest coords if multicast
        NocDestination noc_dest;
        if (noc_event_type == "WRITE_MULTICAST") {
            int64_t mcast_start_x =
                get_with_default(event["mcast_start_x"].get_int64(), int64_t(-1));
            int64_t mcast_start_y =
                get_with_default(event["mcast_start_y"].get_int64(), int64_t(-1));
            int64_t mcast_end_x = get_with_default(event["mcast_end_x"].get_int64(), int64_t(-1));
            int64_t mcast_end_y = get_with_default(event["mcast_end_y"].get_int64(), int64_t(-1));
            if (mcast_start_x == -1 || mcast_start_y == -1 || mcast_end_x == -1 ||
                mcast_end_y == -1) {
                log_error(
                    "Multicast Transfer event missing 'mcast_start_x/y' or 'mcast_end_x/y'; "
                    "skipping ... ");
                continue;
            }
            if (noc_type == "NOC_0") {
                noc_dest = MulticastCoordSet(
                    Coord{dst_device_id, mcast_start_y, mcast_start_x},
                    Coord{dst_device_id, mcast_end_y, mcast_end_x});
            } else if (noc_type == "NOC_1") {
                // NOTE: noc_dest coord are reversed for NOC1
                noc_dest = MulticastCoordSet(
                    Coord{dst_device_id, mcast_end_y, mcast_end_x},
                    Coord{dst_device_id, mcast_start_y, mcast_start_x});
            }
        } else {
            noc_dest = Coord{dst_device_id, dy, dx};
        }

        Coord noc_src_coord{src_device_id, sy, sx};

        // if multichip route override exists, add it to the transfer
        simdjson::dom::object fabric_send_metadata;
        if (event["fabric_send"].get(fabric_send_metadata) == simdjson::SUCCESS) {
            npeWorkloadTransferGroupID transfer_group_id = wl.registerTransferGroupID();
            npeWorkloadTransferGroupIndex transfer_group_index = 0;

            simdjson::dom::array fabric_path;
            if (fabric_send_metadata["path"].get(fabric_path) == simdjson::SUCCESS) {
                // log("Fabric Path src_device={},{},{} dst_device={},{},{} ({} hops)",
                //     src_device_id,
                //     sx,
                //     sy,
                //     dst_device_id,
                //     dx,
                //     dy,
                //     hops);

                for (const auto &route : fabric_path) {
                    std::string_view noc_type_str =
                        get_with_default(route["noc"].get_string(), std::string_view{""});
                    nocType noc_type = noc_type_str == "NOC_0" ? nocType::NOC0 : nocType::NOC1;
                    DeviceID route_segment_device_id =
                        get_with_default(route["device"].get_int64(), int64_t(-1));
                    int64_t segment_start_x =
                        get_with_default(route["segment_start_x"].get_int64(), int64_t(-1));
                    int64_t segment_start_y =
                        get_with_default(route["segment_start_y"].get_int64(), int64_t(-1));
                    int64_t forward_x =
                        get_with_default(route["forward_x"].get_int64(), int64_t(-1));
                    int64_t forward_y =
                        get_with_default(route["forward_y"].get_int64(), int64_t(-1));
                    int64_t parent_id =
                        get_with_default(route["parent_id"].get_int64(), int64_t(-1));
                    simdjson::dom::array local_writes;
                    simdjson::error_code contains_local_writes = route["local_writes"].get(local_writes);

                    // add latency for first route (latencies for remaining routes are added in npeEngine::genDependencies)
                    // NOTE: all fabric events are writes!
                    if (transfer_group_index == 0) {
                        switch (device_model->getArch()) {
                            case DeviceArch::WormholeB0:
                                phase_cycle_offset += WormholeB0DeviceModel::get_write_latency(segment_start_x, segment_start_y, 
                                    forward_x, forward_y, noc_type_str);
                                break;
                            case DeviceArch::Blackhole:
                                phase_cycle_offset += BlackholeDeviceModel::get_write_latency(segment_start_x, segment_start_y, 
                                    forward_x, forward_y, noc_type_str);
                            default:
                                log_error("Unknown device model: {}", device_name);
                                throw npeException(npeErrorCode::TRACE_INGEST_FAILED);
                        }
                    }
                    
                    if (route_segment_device_id == -1 || segment_start_x == -1 || segment_start_y == -1 || 
                        (contains_local_writes != simdjson::SUCCESS && (forward_x == -1 || forward_y == -1))) {
                        log_error(
                            "Transfer at timestamp {} (origin device={} x={} y={}) has one or more "
                            "missing fields in fabric send path; skipping ... ",
                            ts,
                            src_device_id,
                            sx,
                            sy);
                        continue;
                    }

                    // log("    Route Segment ({},{},{}) -> ({},{},{}) ",
                    //     route_segment_device_id,
                    //     segment_start_x,
                    //     segment_start_y,
                    //     route_segment_device_id,
                    //     segment_end_x,
                    //     segment_end_y);

                    // Both writes to local worker cores and forward to downstream ethernet core depend on 
                    // the previous forward hence the same transfer_group_parent
                    if (contains_local_writes == simdjson::SUCCESS) {
                        for (auto local_write: local_writes) {
                            int64_t local_write_x =
                                get_with_default(local_write["dx"].get_int64(), int64_t(-1));
                            int64_t local_write_y =
                                get_with_default(local_write["dy"].get_int64(), int64_t(-1));
                            int64_t local_write_bytes =
                                get_with_default(local_write["num_bytes"].get_int64(), int64_t(-1));

                            if (local_write_x != -1 && local_write_y != -1 && local_write_bytes != -1) {
                                phase.transfers.emplace_back(
                                    local_write_bytes,
                                    1,
                                    Coord{route_segment_device_id, segment_start_y, segment_start_x},
                                    Coord{route_segment_device_id, local_write_y, local_write_x},
                                    0.0,
                                    phase_cycle_offset,
                                    noc_type,
                                    noc_event_type,
                                    enclosing_zone_path,
                                    transfer_group_id,
                                    transfer_group_index,
                                    parent_id);
                                transfer_group_index++;
                            }
                        }
                    }

                    if (forward_x != -1 && forward_y != -1) {
                        phase.transfers.emplace_back(
                            num_bytes,
                            1,
                            Coord{route_segment_device_id, segment_start_y, segment_start_x},
                            Coord{route_segment_device_id, forward_y, forward_x},
                            0.0,
                            phase_cycle_offset,
                            noc_type,
                            noc_event_type,
                            enclosing_zone_path,
                            transfer_group_id,
                            transfer_group_index,
                            parent_id);
                        transfer_group_index++;
                    }
                }
            }
        } else {
            phase.transfers.emplace_back(
                num_bytes,
                1,
                noc_src_coord,
                noc_dest,
                0.0,
                phase_cycle_offset,
                (noc_type == "NOC_0") ? nocType::NOC0 : nocType::NOC1,
                noc_event_type,
                enclosing_zone_path);
        }
    }
    wl.addPhase(phase);

    wl.setSourceFilePath(input_filepath);

    if (verbose)
        fmt::println("Workload converted in {:.2f} ms", st.getElapsedTimeMicroSeconds() / 1000.0);
    return wl;
}

std::optional<npeWorkload> createWorkloadFromJSON(
    const std::string &wl_filename, const std::string &device_name, bool is_tt_metal_trace_format, bool verbose) {
    try {
        if (is_tt_metal_trace_format) {
            return convertNocTracesToNpeWorkload(wl_filename, device_name, verbose);
        } else {
            auto result = loadJSONWorkloadFormat(wl_filename, verbose);
            if (result.has_value()) {
                return result;
            } else {
                log_warn(
                    "Failed to load workload file; fallback to parsing as tt-metal noc trace ... ");
                return convertNocTracesToNpeWorkload(wl_filename, device_name, verbose);
            }
        }
    } catch (const tt_npe::npeException &exp) {
        tt_npe::log_error("{}", exp.what());
        return std::nullopt;
    }
}

}  // namespace tt_npe

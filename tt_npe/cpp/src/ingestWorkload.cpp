// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "ingestWorkload.hpp"

#include <filesystem>
#include <unordered_set>
#include <map>

#include "ScopedTimer.hpp"
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
    ScopedTimer st;
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
                "Its likely you are trying to load a tt-metal noc trace file; for tt_npe_run use -t to specify a trace file");
            return {};
        }

        size_t golden_cycles = 0;
        if (json_data["golden_result"].error() == simdjson::SUCCESS &&
            json_data["golden_result"]["cycles"].error() == simdjson::SUCCESS) {
            // retrieve golden cycles if it exists
            golden_cycles = json_data["golden_result"]["cycles"].get_uint64().value();
            wl.setGoldenResultCycles(golden_cycles);
        }

        simdjson::dom::array phases;
        if (json_data["phases"].get_array().error() != simdjson::SUCCESS) {
            log_error("No workload phases declared within workload file '{}'!", wl_filename);
            return {};
        }
        phases = json_data["phases"].get_array();

        for (const auto &phase : phases) {
            npeWorkloadPhase ph;
            simdjson::dom::array transfers = phase["transfers"].get_array();
            simdjson::error_code err;
            for (const auto &transfer : transfers) {
                int64_t packet_size, num_packets, src_x, src_y;
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

                // determine if multicast or unicast based on presence of dst_x and dst_y
                NocDestination noc_dest;
                int64_t dst_x, dst_y;
                if (transfer["dst_x"].get_int64().get(dst_x) != simdjson::SUCCESS) {
                    dst_x = -1;
                }
                if (transfer["dst_y"].get_int64().get(dst_y) != simdjson::SUCCESS) {
                    dst_y = -1;
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

                    noc_dest = MCastCoordPair(
                        Coord{mcast_start_y, mcast_start_x}, Coord{mcast_end_y, mcast_end_x});
                } else {
                    noc_dest = Coord(dst_y, dst_x);
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
                    Coord{src_y, src_x},
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

    if (verbose)
        fmt::println("Workload loaded in {} ms", st.getElapsedTimeMilliSeconds());
    return wl;
}

std::optional<npeWorkload> convertNocTracesToNpeWorkload(const std::string &input_filepath, bool verbose) {
    ScopedTimer st;
    npeWorkload wl;

    const std::unordered_set<std::string_view> SUPPORTED_NOC_EVENTS = {
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
        log_error("Encountered parsing error while reading JSON input file '{}': {}", input_filepath, exp.what());
        return {};
    }

    double t0_timestamp = 2e30;
    std::map<std::tuple<std::string_view, int64_t, int64_t>, std::pair<double, double>> per_core_ts;

    for (const auto &event : event_data_json.get_array()) {
        double ts = get_with_default(event["timestamp"].get_int64(), int64_t(0));
        t0_timestamp = std::min(t0_timestamp, ts);

        std::string_view proc = get_with_default(event["proc"].get_string(), std::string_view{});
        int64_t sx = get_with_default(event["sx"].get_int64(), int64_t(-1));
        int64_t sy = get_with_default(event["sy"].get_int64(), int64_t(-1));
        if (proc != "" && sx != -1 && sy != -1) {
            auto key = std::make_tuple(proc, sx, sy);
            auto it = per_core_ts.find(key);
            if (it == per_core_ts.end()) {
                per_core_ts[key] = {ts, ts};
            } else {
                auto& minmax_ts = it->second;
                minmax_ts.first = std::min(minmax_ts.first, ts);
                minmax_ts.second = std::max(minmax_ts.second, ts);
            }
        }
    }

    size_t max_kernel_cycles = 0;
    for (const auto &[key, min_max_ts] : per_core_ts) {
        size_t delta = min_max_ts.second - min_max_ts.first;
        max_kernel_cycles = std::max(max_kernel_cycles, delta);
    }
    // there is at least a ~20 cycle overhead between last noc event and kernel end timestamp
    max_kernel_cycles -= 20;

    wl.setGoldenResultCycles(max_kernel_cycles);

    struct NoCEventSavedState {
        int64_t sx=0;
        int64_t sy=0;
        int64_t dx=0;
        int64_t dy=0;
        int64_t num_bytes=0;
    };

    std::vector<npeWorkloadPhase> phases;
    npeWorkloadPhase phase;
    NoCEventSavedState curr_saved_state_read;
    NoCEventSavedState curr_saved_state_write;
    for (const auto &event : event_data_json.get_array()) {
        std::string_view proc = get_with_default(event["proc"].get_string(), std::string_view{});
        std::string_view noc_event_type = get_with_default(event["type"].get_string(), std::string_view{});
        int64_t num_bytes = get_with_default(event["num_bytes"].get_int64(), int64_t(0));
        int64_t sx = get_with_default(event["sx"].get_int64(), int64_t(-1));
        int64_t sy = get_with_default(event["sy"].get_int64(), int64_t(-1));
        int64_t dx = get_with_default(event["dx"].get_int64(), int64_t(-1));
        int64_t dy = get_with_default(event["dy"].get_int64(), int64_t(-1));

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
        if (noc_event_type.starts_with("READ")){
            std::swap(sx, dx);
            std::swap(sy, dy);
        }

        // XXX : this is hardcoded to wormhole_b0 latencies! 
        double ts = get_with_default(event["timestamp"].get_int64(), int64_t(0));
        int64_t phase_cycle_offset = ts - t0_timestamp;
        if (sx == dx && sy == dy) {
            phase_cycle_offset += 70;
        } else if (sx == dx && sy != dy) {
            phase_cycle_offset += 154;
        } else if (sy == dy && sx != dx) {
            phase_cycle_offset += 170;
        } else {
            phase_cycle_offset += 270;
        }

        std::string_view noc_type =
            get_with_default(event["noc"].get_string(), std::string_view{""});
        if (noc_type.empty()) {
            log_error("No NoC type specified for event; skipping ...");
            continue;
        }

        NocDestination noc_dest;
        if (noc_event_type == "WRITE_MULTICAST") {
            int64_t mcast_start_x = get_with_default(event["mcast_start_x"].get_int64(), int64_t(-1));
            int64_t mcast_start_y = get_with_default(event["mcast_start_y"].get_int64(), int64_t(-1));
            int64_t mcast_end_x = get_with_default(event["mcast_end_x"].get_int64(), int64_t(-1));
            int64_t mcast_end_y = get_with_default(event["mcast_end_y"].get_int64(), int64_t(-1));
            if (mcast_start_x == -1 || mcast_start_y == -1 || mcast_end_x == -1 || mcast_end_y == -1) {
                log_error("Multicast Transfer event missing 'mcast_start_x/y' or 'mcast_end_x/y'; skipping ... ");
                continue;
            }
            if (noc_type == "NOC_0") {
                noc_dest = MCastCoordPair(
                    Coord{mcast_start_y, mcast_start_x}, Coord{mcast_end_y, mcast_end_x});
            } else if (noc_type == "NOC_1") {
                noc_dest = MCastCoordPair(
                    Coord{mcast_end_y, mcast_end_x}, Coord{mcast_start_y, mcast_start_x});
            }
        } else {
            noc_dest = Coord(dy, dx);
        }

        phase.transfers.emplace_back(
            num_bytes,
            1,
            Coord{sy, sx},
            noc_dest,
            0.0,
            phase_cycle_offset,
            (noc_type == "NOC_0") ? nocType::NOC0 : nocType::NOC1,
            noc_event_type
        );
    }
    wl.addPhase(phase);

    if (verbose)
        fmt::println("Workload converted in {:.2f} ms", st.getElapsedTimeMicroSeconds()/1000.0);
    return wl;
}

std::optional<npeWorkload> createWorkloadFromJSON(const std::string &wl_filename, bool is_tt_metal_trace_format, bool verbose) {
    if (is_tt_metal_trace_format) {
        return convertNocTracesToNpeWorkload(wl_filename, verbose);
    } else {
        auto result = loadJSONWorkloadFormat(wl_filename, verbose);
        if (result.has_value()) {
            return result;
        } else {
            log_warn("Failed to load workload file; fallback to parsing as tt-metal noc trace ... ");
            return convertNocTracesToNpeWorkload(wl_filename, verbose);
        }
    }
}


}  // namespace tt_npe

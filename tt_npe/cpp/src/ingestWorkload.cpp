// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "ingestWorkload.hpp"

#include <filesystem>

#include "ScopedTimer.hpp"
#include "npeUtil.hpp"
#include "npeWorkload.hpp"
#include "simdjson.h"

namespace tt_npe {

std::optional<npeWorkload> ingestJSONWorkload(const std::string &wl_filename, bool verbose) {
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
                    log_error(
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

                ph.transfers.emplace_back(
                    packet_size,
                    num_packets,
                    // note: row is y position, col is x position!
                    Coord{src_y, src_x},
                    noc_dest,
                    injection_rate,
                    phase_cycle_offset,
                    (noc_type == "NOC_0") ? nocType::NOC0 : nocType::NOC1);
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
}  // namespace tt_npe

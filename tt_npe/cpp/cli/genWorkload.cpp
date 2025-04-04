// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "genWorkload.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

#include "yaml-cpp/yaml.h"
#include "fmt/base.h"

#include "grid.hpp"
#include "npeWorkload.hpp"
#include "npeUtil.hpp"

tt_npe::npeWorkload genRandomizedWorkload(
    const tt_npe::npeDeviceModel &model, const boost::unordered::unordered_flat_map<std::string, float> &params) {
    tt_npe::npeWorkload wl;

    const int num_transfers = tt_npe::getWithDefault(params, "num_transfers", 1.0f);
    const size_t packet_size = tt_npe::getWithDefault(params, "packet_size", 1.0f);
    const size_t injection_rate = tt_npe::getWithDefault(params, "injection_rate", 1.0f);
    const size_t num_packets = tt_npe::getWithDefault(params, "num_packets", 1.0f);

    // construct one big phas with a bunch of random transfers
    tt_npe::npeWorkloadPhase ph;
    ph.transfers.reserve(num_transfers);
    size_t total_bytes_overall = 0;
    tt_npe::Grid2D<int> transfer_per_src_loc(model.getRows(), model.getCols());
    for (int i = 0; i < num_transfers; i++) {
        auto src = tt_npe::Coord{tt_npe::DeviceID(), rand() % 2, (rand() % 2)};
        auto dst = tt_npe::Coord{tt_npe::DeviceID(), rand() % model.getRows(), (rand() % model.getCols())};
        tt_npe::CycleCount startup_latency =
            (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
        startup_latency +=
            transfer_per_src_loc(src.row, src.col) * ((packet_size * num_packets) / injection_rate);
        startup_latency += rand() % 32;
        transfer_per_src_loc(src.row, src.col)++;

        total_bytes_overall += packet_size * num_packets;

        ph.transfers.emplace_back(
            packet_size,
            num_packets,
            src,
            dst,
            injection_rate,
            startup_latency,
            tt_npe::nocType::NOC1);
    }
    fmt::println(
        "{} total bytes in all transfers; {} per Tensix",
        total_bytes_overall,
        total_bytes_overall / 120);

    wl.addPhase(std::move(ph));

    return wl;
}

tt_npe::npeWorkload gen2DReshardWorkload(
    const tt_npe::npeDeviceModel &model, const boost::unordered_flat_map<std::string, float> &params) {
    tt_npe::npeWorkload wl;

    const size_t packet_size = tt_npe::getWithDefault(params, "packet_size", 1.0f);
    const size_t num_packets = tt_npe::getWithDefault(params, "num_packets", 1.0f);
    const size_t injection_rate = tt_npe::getWithDefault(params, "injection_rate", 1.0f);

    // construct one big phas with a bunch of random transfers
    tt_npe::npeWorkloadPhase ph;
    tt_npe::Grid2D<int> transfer_per_src_loc(model.getRows(), model.getCols());
    size_t total_bytes_overall = 0;

    std::vector<tt_npe::Coord> destinations;
    for (int row : {1, 2, 3, 4}) {
        for (int col : {1, 2, 3, 4}) {
            destinations.push_back({tt_npe::DeviceID(), row, col});
        }
    }

    for (auto [device_id, row, col] : destinations) {
        auto dst = tt_npe::Coord{tt_npe::DeviceID(), row, col};
        auto src = tt_npe::Coord{tt_npe::DeviceID(), (row + 1) / 2, (col + 1) / 2};

        fmt::println(
            "Read of {} {}K packets going from src:{} to dst:{}",
            num_packets,
            packet_size / 1024,
            src,
            dst);

        tt_npe::CycleCount startup_latency =
            (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
        total_bytes_overall += packet_size * num_packets;

        ph.transfers.emplace_back(
            packet_size,
            num_packets,
            src,
            dst,
            injection_rate,
            startup_latency,
            tt_npe::nocType::NOC0);
        ph.transfers.emplace_back(
            packet_size,
            num_packets,
            src,
            dst,
            injection_rate,
            startup_latency,
            tt_npe::nocType::NOC1);
    }

    wl.addPhase(std::move(ph));

    return wl;
}

tt_npe::npeWorkload genCongestedWorkload(
    const tt_npe::npeDeviceModel &model, const boost::unordered_flat_map<std::string, float> &params) {
    tt_npe::npeWorkload wl;

    const int num_transfers = tt_npe::getWithDefault(params, "num_transfers", 1.0f);
    const size_t packet_size = tt_npe::getWithDefault(params, "packet_size", 1.0f);
    const size_t num_packets = tt_npe::getWithDefault(params, "num_packets", 1.0f);
    const size_t injection_rate = tt_npe::getWithDefault(params, "injection_rate", 1.0f);

    // construct one big phas with a bunch of random transfers
    tt_npe::npeWorkloadPhase ph;
    tt_npe::Grid2D<int> transfer_per_src_loc(model.getRows(), model.getCols());
    ph.transfers.reserve(num_transfers);
    size_t total_bytes_overall = 0;
    for (int i = 0; i < num_transfers; i++) {
        auto src = tt_npe::Coord{tt_npe::DeviceID(), 1, i + 1};
        auto dst = tt_npe::Coord{tt_npe::DeviceID(), 1, 10};

        tt_npe::CycleCount startup_latency =
            (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
        startup_latency += ((i % 2) == 0) ? 10 : 0;
        total_bytes_overall += packet_size * num_packets;

        ph.transfers.emplace_back(
            packet_size,
            num_packets,
            src,
            dst,
            injection_rate,
            startup_latency,
            tt_npe::nocType::NOC0);
    }

    wl.addPhase(std::move(ph));

    return wl;
}

tt_npe::npeWorkload genSingleTransferWorkload(
    const tt_npe::npeDeviceModel &model, const boost::unordered_flat_map<std::string, float> &params) {
    tt_npe::npeWorkload wl;

    const int NUM_TRANSFERS = 1;
    size_t packet_size = tt_npe::getWithDefault(params, "packet_size", 1.0f);

    // construct one big phas with a bunch of random transfers
    tt_npe::npeWorkloadPhase ph;
    size_t byte_sum = 0;
    for (int i = 0; i < NUM_TRANSFERS; i++) {
        int src_x = tt_npe::getWithDefault(params, "src_x", 1.0f);
        int src_y = tt_npe::getWithDefault(params, "src_y", 1.0f);
        int dst_x = tt_npe::getWithDefault(params, "dst_x", 1.0f);
        int dst_y = tt_npe::getWithDefault(params, "dst_y", 1.0f);

        auto src = tt_npe::Coord{tt_npe::DeviceID(), src_y, src_x};
        auto dst = tt_npe::Coord{tt_npe::DeviceID(), dst_y, dst_x};

        tt_npe::CycleCount startup_latency =
            tt_npe::getWithDefault(params, "startup_latency", 155.0f);
        float injection_rate = tt_npe::getWithDefault(params, "injection_rate", 28.1f);
        int num_packets = tt_npe::getWithDefault(params, std::string("num_packets"), 1.0f);

        ph.transfers.emplace_back(
            packet_size,
            num_packets,
            src,
            dst,
            injection_rate,
            startup_latency,
            tt_npe::nocType::NOC0);
    }

    wl.addPhase(std::move(ph));

    return wl;
}
tt_npe::npeWorkload genTestWorkload(
    const tt_npe::npeDeviceModel &model, const std::string &workload_config_file, bool verbose) {
    boost::unordered_flat_map<std::string, float> params;

    // load config file
    auto yaml_cfg = YAML::LoadFile(workload_config_file);

    auto test_name = yaml_cfg["test_name"].as<std::string>();
    if (verbose) {
        fmt::println("test config {}", test_name);
    }
    for (const auto &item : yaml_cfg["test_params"]) {
        std::string param = item.first.as<std::string>();
        float value = item.second.as<float>();
        params[param] = value;
        if (verbose) {
            fmt::println("    {:16s} {:4}", param + ":", value);
        }
    }

    if (test_name == "random") {
        return genRandomizedWorkload(model, params);
    } else if (test_name == "1d-congestion") {
        return genCongestedWorkload(model, params);
    } else if (test_name == "2d-reshard") {
        return gen2DReshardWorkload(model, params);
    } else if (test_name == "single-transfer") {
        return genSingleTransferWorkload(model, params);
    } else {
        tt_npe::log_error("test name '{}' is not defined!", test_name);
        return tt_npe::npeWorkload{};
    }
}

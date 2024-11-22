#include "genWorkload.hpp"

#include <unordered_map>

#include "grid.hpp"
#include "util.hpp"

tt_npe::nocWorkload genRandomizedWorkload(
    const tt_npe::nocModel &model, const std::unordered_map<std::string, float> &params) {
    tt_npe::nocWorkload wl;

    const int num_transfers = tt_npe::getWithDefault(params, "num_transfers", 1.0f);
    const size_t packet_size = tt_npe::getWithDefault(params, "packet_size", 1.0f);
    const size_t injection_rate = tt_npe::getWithDefault(params, "injection_rate", 1.0f);
    const size_t num_packets = tt_npe::getWithDefault(params, "num_packets", 1.0f);

    // construct one big phas with a bunch of random transfers
    tt_npe::nocWorkloadPhase ph;
    ph.transfers.reserve(num_transfers);
    size_t total_bytes_overall = 0;
    CycleCount stagger = 0;
    tt_npe::Grid2D<int> transfer_per_src_loc(model.getRows(),model.getCols());
    for (int i = 0; i < num_transfers; i++) {
        auto src = tt_npe::Coord{rand() % 2, (rand() % 2)};
        auto dst = tt_npe::Coord{rand() % model.getRows(), (rand() % model.getCols())};
        CycleCount startup_latency = (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
        startup_latency += transfer_per_src_loc(src.row,src.col) * ((packet_size*num_packets)/injection_rate);
        transfer_per_src_loc(src.row,src.col)++;

        total_bytes_overall += packet_size * num_packets;

        tt_npe::nocWorkloadTransfer tr{
            .packet_size = packet_size,
            .num_packets = num_packets,
            .src = src,
            .dst = dst,
            .injection_rate = injection_rate,
            .cycle_offset = startup_latency};
        ph.transfers.push_back(tr);
    }
    fmt::println("{} total bytes in all transfers; {} per Tensix", total_bytes_overall, total_bytes_overall / 120);

    wl.addPhase(std::move(ph));

    return wl;
}

tt_npe::nocWorkload genCongestedWorkload(
    const tt_npe::nocModel &model, const std::unordered_map<std::string, float> &params) {
    tt_npe::nocWorkload wl;

    const int NUM_TRANSFERS = tt_npe::getWithDefault(params, "num_transfers", 1.0f);
    const size_t PACKET_SIZE = tt_npe::getWithDefault(params, "packet_size", 1.0f);
    const size_t num_packets = tt_npe::getWithDefault(params, "num_packets", 1.0f);
    const size_t injection_rate = tt_npe::getWithDefault(params, "injection_rate", 1.0f);

    // construct one big phas with a bunch of random transfers
    tt_npe::nocWorkloadPhase ph;
    ph.transfers.reserve(NUM_TRANSFERS);
    size_t total_bytes_overall = 0;
    for (int i = 0; i < NUM_TRANSFERS; i++) {
        auto src = tt_npe::Coord{1, i + 1};
        auto dst = tt_npe::Coord{1, 10};

        CycleCount startup_latency = (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
        total_bytes_overall += PACKET_SIZE * num_packets;

        tt_npe::nocWorkloadTransfer tr{
            .packet_size = PACKET_SIZE,
            .num_packets = num_packets,
            .src = src,
            .dst = dst,
            .injection_rate = injection_rate,
            .cycle_offset = startup_latency};
        ph.transfers.push_back(tr);
    }

    wl.addPhase(std::move(ph));

    return wl;
}

tt_npe::nocWorkload genSingleTransferWorkload(
    const tt_npe::nocModel &model, const std::unordered_map<std::string, float> &params) {
    tt_npe::nocWorkload wl;

    const int NUM_TRANSFERS = 1;
    size_t packet_size = tt_npe::getWithDefault(params, "packet_size", 1.0f);

    // construct one big phas with a bunch of random transfers
    tt_npe::nocWorkloadPhase ph;
    size_t byte_sum = 0;
    for (int i = 0; i < NUM_TRANSFERS; i++) {
        int src_x = tt_npe::getWithDefault(params, "src_x", 1.0f);
        int src_y = tt_npe::getWithDefault(params, "src_y", 1.0f);
        int dst_x = tt_npe::getWithDefault(params, "dst_x", 1.0f);
        int dst_y = tt_npe::getWithDefault(params, "dst_y", 1.0f);

        auto src = tt_npe::Coord{src_x, src_y};
        auto dst = tt_npe::Coord{dst_x, dst_y};

        CycleCount startup_latency = tt_npe::getWithDefault(params, "startup_latency", 155.0f);
        float injection_rate = tt_npe::getWithDefault(params, "injection_rate", 28.1f);
        int num_packets = tt_npe::getWithDefault(params, std::string("num_packets"), 1.0f);
        tt_npe::nocWorkloadTransfer tr{
            .packet_size = packet_size,
            .num_packets = num_packets,
            .src = src,
            .dst = dst,
            .injection_rate = injection_rate,
            .cycle_offset = startup_latency,
        };
        ph.transfers.push_back(tr);
    }

    wl.addPhase(std::move(ph));

    return wl;
}
tt_npe::nocWorkload genTestWorkload(const tt_npe::nocModel &model, const YAML::Node &yaml_cfg) {
    std::unordered_map<std::string, float> params;
    auto test_name = yaml_cfg["test_name"].as<std::string>();
    tt_npe::printDiv(std::string("test config " + test_name));
    for (const auto &item : yaml_cfg["test_params"]) {
        std::string param = item.first.as<std::string>();
        float value = item.second.as<float>();
        params[param] = value;
        fmt::println(" {:14s} {}", param + ":", value);
    }
    tt_npe::printDiv();

    if (test_name == "random") {
        return genRandomizedWorkload(model, params);
    } else if (test_name == "1d-congestion") {
        return genCongestedWorkload(model, params);
    } else if (test_name == "single-transfer") {
        return genSingleTransferWorkload(model, params);
    } else {
        fmt::println("test name '{}' is not defined!", test_name);
        return tt_npe::nocWorkload{};
    }
}

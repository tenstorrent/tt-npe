#include "genWorkload.hpp"
#include "util.hpp"
#include <unordered_map>

tt_npe::nocWorkload
genRandomizedWorkload(const tt_npe::nocModel &model,
                      const std::unordered_map<std::string, float> &params) {
  tt_npe::nocWorkload wl;

  constexpr int NUM_TRANSFERS = 256;
  constexpr size_t PACKET_SIZE = 8192;

  // construct one big phas with a bunch of random transfers
  tt_npe::nocWorkloadPhase ph;
  ph.transfers.reserve(NUM_TRANSFERS);
  size_t total_bytes_overall = 0;
  for (int i = 0; i < NUM_TRANSFERS; i++) {
    auto src =
        tt_npe::Coord{(rand() % model.getRows()), (rand() % model.getCols())};
    auto dst =
        tt_npe::Coord{(rand() % model.getRows()), (rand() % model.getCols())};
    CycleCount startup_latency =
        (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
    startup_latency += rand() % 512;
    auto bytes = ((rand() % 8) + 2) * PACKET_SIZE;
    total_bytes_overall += bytes;
    tt_npe::nocWorkloadTransfer tr{.bytes = bytes,
                                   .packet_size = PACKET_SIZE,
                                   .src = src,
                                   .dst = dst,
                                   .cycle_offset = startup_latency};
    ph.transfers.push_back(tr);
  }
  fmt::println("{} total bytes in all transfers; {} per Tensix",
               total_bytes_overall, total_bytes_overall / 120);

  wl.addPhase(std::move(ph));

  return wl;
}

tt_npe::nocWorkload genSingleTransferWorkload(
    const tt_npe::nocModel &model,
    const std::unordered_map<std::string, float> &params) {
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

    CycleCount startup_latency =
        (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
    int num_packets =
        tt_npe::getWithDefault(params, std::string("num_packets"), 1.0f);
    size_t bytes = num_packets * packet_size;
    byte_sum += bytes;
    tt_npe::nocWorkloadTransfer tr{.bytes = bytes,
                                   .packet_size = packet_size,
                                   .src = src,
                                   .dst = dst,
                                   .cycle_offset = startup_latency};
    ph.transfers.push_back(tr);
  }
  fmt::println("{} total bytes", byte_sum);

  wl.addPhase(std::move(ph));

  return wl;
}
tt_npe::nocWorkload genTestWorkload(const tt_npe::nocModel &model,
                                    const YAML::Node &yaml_cfg) {

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
  } else if (test_name == "single-transfer") {
    return genSingleTransferWorkload(model, params);
  } else {
    fmt::println("test name '{}' is not defined!", test_name);
    return tt_npe::nocWorkload{};
  }
}
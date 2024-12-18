#include "ingestWorkload.hpp"

#include "ScopedTimer.hpp"
#include "npeWorkload.hpp"
#include "util.hpp"
#include "yaml-cpp/yaml.h"

namespace tt_npe {

npeWorkload ingestYAMLWorkload(const std::string &yaml_wl_filename) {
    ScopedTimer st;
    npeWorkload wl;

    // load config file
    auto yaml_cfg = YAML::LoadFile(yaml_wl_filename);

    for (const auto &phase : yaml_cfg["phases"]) {
        assert(phase.second.IsMap());
        fmt::println("phase {}", phase.first.as<std::string>());
        npeWorkloadPhase ph;
        for (const auto &transfer : phase.second["transfers"]) {
            // fmt::println("  transfer {}", transfer.first.as<std::string>());
            auto packet_size = transfer.second["packet_size"].as<int>(0);
            auto num_packets = transfer.second["num_packets"].as<int>(0);
            auto src_x = transfer.second["src_x"].as<int>(0);
            auto src_y = transfer.second["src_y"].as<int>(0);
            auto dst_x = transfer.second["dst_x"].as<int>(0);
            auto dst_y = transfer.second["dst_y"].as<int>(0);
            auto injection_rate = transfer.second["injection_rate"].as<float>(0.0f);
            auto phase_cycle_offset = transfer.second["phase_cycle_offset"].as<int>(0);
            auto noc_type = transfer.second["noc_type"].as<std::string>("NOC_0");

            ph.transfers.emplace_back(
                packet_size,
                num_packets,
                // note: row is y position, col is x position!
                Coord{src_y, src_x},
                Coord{dst_y, dst_x},
                injection_rate,
                phase_cycle_offset,
                (noc_type == "NOC_0") ? nocType::NOC0 : nocType::NOC1);

            // fmt::println(
            //     "{} {} {} {} {} {} {} {}",
            //     packet_size,
            //     src_x,
            //     src_y,
            //     dst_x,
            //     dst_y,
            //     injection_rate,
            //     phase_cycle_offset,
            //     noc_type);
        }
        wl.addPhase(ph);
    }

    fmt::println("workload ingestion took {} us", st.getElapsedTimeMicroSeconds());
    return wl;
}
}  // namespace tt_npe
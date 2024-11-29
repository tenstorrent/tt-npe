#pragma once
#include <string>

namespace tt_npe {

// Enum for verbosity levels
enum class VerbosityLevel { Normal = 0, Verbose = 1, MoreVerbose = 2, MostVerbose = 3 };

// common config fields (populated from cli options or via API)
struct npeConfig {
    std::string device_name;
    std::string congestion_model_name;
    std::string workload_yaml;
    std::string test_config_yaml;
    uint32_t cycles_per_timestep = 0;
    VerbosityLevel verbosity = VerbosityLevel::Normal;
    bool enable_visualizations = false;
};

}  // namespace tt_npe

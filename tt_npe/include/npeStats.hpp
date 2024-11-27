#pragma once

#include <string>

namespace tt_npe {

// various results from npe simulation
struct npeStats {
    bool completed = false;
    size_t estimated_cycles = 0;
    size_t simulated_cycles = 0;
    size_t num_timesteps = 0;
    size_t wallclock_runtime_us = 0;
    std::string to_string(bool verbose = false) const;
};

}  // namespace tt_npe
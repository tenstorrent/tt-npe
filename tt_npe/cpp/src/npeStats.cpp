#include "npeStats.hpp"

#include "fmt/core.h"

namespace tt_npe {
std::string npeStats::to_string(bool verbose) const {
    std::string output;

    output.append(fmt::format(" estimated cycles: {:5d}\n", estimated_cycles));
    output.append(fmt::format(" cycle pred error: {:5.1f}%\n", cycle_prediction_error));

    output.append("\n");
    output.append(fmt::format("    avg Link util: {:5.0f}%\n", overall_avg_link_util));
    output.append(fmt::format("    max Link util: {:5.0f}%\n", overall_max_link_util));
    output.append("\n");
    output.append(fmt::format("  avg Link demand: {:5.0f}%\n", overall_avg_link_demand));
    output.append(fmt::format("  max Link demand: {:5.0f}%\n", overall_max_link_demand));
    output.append("\n");
    output.append(fmt::format("  avg NIU  demand: {:5.0f}%\n", overall_avg_niu_demand));
    output.append(fmt::format("  max NIU  demand: {:5.0f}%\n", overall_max_niu_demand));

    if (verbose) {
        output.append("\n");
        output.append(fmt::format("    num timesteps: {:5d}\n", num_timesteps));
        output.append(fmt::format("   wallclock time: {:5d} us\n", wallclock_runtime_us));
    }
    return output;
}
}  // namespace tt_npe
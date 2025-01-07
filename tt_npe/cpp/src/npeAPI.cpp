// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeAPI.hpp"

#include "npeCommon.hpp"
#include "npeEngine.hpp"

namespace tt_npe {

npeAPI::npeAPI(const tt_npe::npeConfig &cfg) : cfg(cfg), engine(cfg.device_name) {
    validateConfig();  // throws npeException if config is invalid!
}

void npeAPI::validateConfig() const {
    if (cfg.cycles_per_timestep <= 0) {
        throw npeException(
            npeErrorCode::INVALID_CONFIG,
            fmt::format("Illegal cycles per timestep '{}' in npeConfig", cfg.cycles_per_timestep));
    }
    if (cfg.congestion_model_name != "none" && cfg.congestion_model_name != "fast") {
        throw npeException(
            npeErrorCode::INVALID_CONFIG,
            fmt::format(
                "Illegal congestion model name '{}' in npeConfig", cfg.congestion_model_name));
    }
}

npeResult npeAPI::runNPE(npeWorkload wl) const {
    bool verbose = cfg.verbosity != VerbosityLevel::Normal;
    if (cfg.infer_injection_rate_from_src) {
        wl.inferInjectionRates(engine.getDeviceModel());
    }
    if (not wl.validate(engine.getDeviceModel(), true)) {
        return npeException(npeErrorCode::WORKLOAD_VALIDATION_FAILED);
    }
    return engine.runPerfEstimation(wl, cfg);
}

}  // namespace tt_npe

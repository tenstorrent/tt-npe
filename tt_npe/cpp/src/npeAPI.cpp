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
    if (!npeConfig::isValidDramControllerModel(cfg.dram_controller_model)) {
        throw npeException(
            npeErrorCode::INVALID_CONFIG,
            fmt::format(
                "Illegal dram controller model '{}' in npeConfig; expected one of "
                "'off', 'observe', 'enforce'",
                cfg.dram_controller_model));
    }
    if (cfg.dram_controller_capacity_scale <= 0.0f) {
        throw npeException(
            npeErrorCode::INVALID_CONFIG,
            fmt::format(
                "Illegal dram controller capacity scale '{}' in npeConfig; must be > 0",
                cfg.dram_controller_capacity_scale));
    }
}

npeWorkload npeAPI::preprocessWorkload(npeWorkload wl) const {
    if (cfg.infer_injection_rate_from_src) {
        wl.inferInjectionRates(engine.getDeviceModel());
    }
    if (cfg.scale_workload_schedule != 0.0f) {
        wl.scaleWorkloadSchedule(cfg.scale_workload_schedule);
    }
    if (cfg.remove_localized_unicast_transfers) {
        wl = wl.removeLocalUnicastTransfers();
    }
    return wl;
}

npeResult npeAPI::runNPE(npeWorkload wl) const {
    bool verbose = cfg.verbosity > VerbosityLevel::Normal;

    wl = preprocessWorkload(wl);
    if (not wl.validate(engine.getDeviceModel(), verbose)) {
        return npeException(npeErrorCode::WORKLOAD_VALIDATION_FAILED);
    }
    try {
        return engine.runPerfEstimation(wl, cfg);
    } catch (const npeException &exp) {
        return exp;
    }
}

}  // namespace tt_npe

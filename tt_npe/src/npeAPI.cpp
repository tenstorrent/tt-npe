#include "npeAPI.hpp"

#include "fmt/base.h"
#include "npeEngine.hpp"

namespace tt_npe {

std::optional<npeAPI> npeAPI::makeAPI(const tt_npe::npeConfig &cfg) {
    if (auto optional_engine = npeEngine::makeEngine(cfg.device_name)) {
        npeAPI npe_api;
        npe_api.engine = optional_engine.value();
        npe_api.cfg = cfg;
        return npe_api;
    } else {
        log_error("Failed to initialize npeAPI; check error messages and npeConfig arguments.");
        return {};
    }
}

npeResult npeAPI::runNPE(npeWorkload wl) {
    bool verbose = cfg.verbosity != VerbosityLevel::Normal;
    if (cfg.infer_injection_rate_from_src){
        wl.inferInjectionRates(engine.getDeviceModel());
    }
    if (not wl.validate(engine.getDeviceModel(), verbose)) {
        return npeException(npeErrorCode::WORKLOAD_VALIDATION_FAILED);
    }
    return engine.runPerfEstimation(wl, cfg);
}

}  // namespace tt_npe

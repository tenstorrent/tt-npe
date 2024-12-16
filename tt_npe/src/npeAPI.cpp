#include "npeAPI.hpp"

#include "fmt/base.h"
#include "npeCommon.hpp"
#include "npeEngine.hpp"

namespace tt_npe {

npeAPI::npeAPI(const tt_npe::npeConfig &cfg) : cfg(cfg), engine(cfg.device_name) {}

npeResult npeAPI::runNPE(npeWorkload wl) {
    bool verbose = cfg.verbosity != VerbosityLevel::Normal;
    if (cfg.infer_injection_rate_from_src) {
        wl.inferInjectionRates(engine.getDeviceModel());
    }
    if (not wl.validate(engine.getDeviceModel(), verbose)) {
        return npeException(npeErrorCode::WORKLOAD_VALIDATION_FAILED);
    }
    return engine.runPerfEstimation(wl, cfg);
}

}  // namespace tt_npe

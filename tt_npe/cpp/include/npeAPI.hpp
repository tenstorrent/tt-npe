// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include "ingestWorkload.hpp"
#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeEngine.hpp"
#include "npeStats.hpp"
#include "npeWorkload.hpp"

namespace tt_npe {

class npeAPI {
   public:
    npeAPI() = default;

    // main constructor : throws npeException if config is invalid
    npeAPI(const tt_npe::npeConfig &cfg);

    // Run a performance estimation sim and reports back result
    //   if successful, the result will be a npeStats struct
    //   else, the result will be a npeError struct
    tt_npe::npeResult runNPE(tt_npe::npeWorkload wl) const;

    // returns underlying device model used by sim engine
    const npeDeviceModel &getDeviceModel() const { return engine.getDeviceModel(); }

   private:
    // preprocess workload based on config flags
    npeWorkload preprocessWorkload(npeWorkload wl) const;

    // throws npeException if config is invalid
    void validateConfig() const;

    tt_npe::npeConfig cfg;
    tt_npe::npeEngine engine;
};

}  // namespace tt_npe

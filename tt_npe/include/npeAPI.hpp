#include <npeCommon.hpp>
#include <npeConfig.hpp>
#include <npeStats.hpp>
#include <npeWorkload.hpp>

#include "npeDeviceModel.hpp"
#include "npeEngine.hpp"

namespace tt_npe {

class npeAPI {
   public:
    npeAPI() = default;
    npeAPI(const tt_npe::npeAPI &api) = default;
    npeAPI &operator=(const tt_npe::npeAPI &api) = default;
    npeAPI(tt_npe::npeAPI &&api) = default;
    npeAPI &operator=(tt_npe::npeAPI &&api) = default;

    // main constructor : throws npeException if config is invalid
    npeAPI(const tt_npe::npeConfig &cfg);

    // Run a performance estimation sim and reports back result
    //   if successful, the result will be a npeStats struct
    //   else, the result will be a npeError struct
    tt_npe::npeResult runNPE(tt_npe::npeWorkload wl) const;

    // returns underlying device model used by sim engine
    const npeDeviceModel &getDeviceModel() const { return engine.getDeviceModel(); }

   private:
    // throws npeException if config is invalid
    void validateConfig() const;

    tt_npe::npeConfig cfg;
    tt_npe::npeEngine engine;
};

}  // namespace tt_npe
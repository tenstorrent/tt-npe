#include <npeConfig.hpp>
#include <npeStats.hpp>
#include <npeWorkload.hpp>

class npeAPI {
    // run a performance estimation sim and reports back stats
    tt_npe::npeStats runPerfEstimation(const tt_npe::npeWorkload &wl, const tt_npe::npeConfig &cfg);
};
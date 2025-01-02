#include "npeDeviceModel.hpp"
#include "npeWorkload.hpp"

tt_npe::npeWorkload genTestWorkload(
    const tt_npe::npeDeviceModel &model,
    const std::string &workload_config_file,
    bool verbose = false);
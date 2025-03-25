// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeDeviceModelIface.hpp"
#include "npeWorkload.hpp"

tt_npe::npeWorkload genTestWorkload(
    const tt_npe::npeDeviceModel &model,
    const std::string &workload_config_file,
    bool verbose = false);
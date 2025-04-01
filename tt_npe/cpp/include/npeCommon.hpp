// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <fmt/core.h>

#include <magic_enum.hpp>
#include <string>

namespace tt_npe {

using BytesPerCycle = float;
using CycleCount = uint32_t;
using DeviceID = int16_t;

enum class nocType { NOC0 = 0, NOC1 = 1 };

enum class npeErrorCode {
    UNDEF = 0,
    WORKLOAD_VALIDATION_FAILED = 1,
    EXCEEDED_SIM_CYCLE_LIMIT = 2,
    INVALID_CONFIG = 3,
    DEVICE_MODEL_INIT_FAILED = 4,
    SIM_ENGINE_INIT_FAILED = 5
};

enum CoreType {
    UNDEF = 0,
    WORKER = 1,
    DRAM = 2,
    ETH = 3,
};

class npeException : std::exception {
   public:
    npeException(const npeException &error) = default;

    npeException(npeErrorCode err_code, std::string msg = "") : err_code(err_code), msg(msg) {}

    const char *what() const noexcept {
        format_buf = msg.empty() ? 
            fmt::format("{}", magic_enum::enum_name(err_code)) :
            fmt::format("{} - {}", magic_enum::enum_name(err_code), msg);
        return format_buf.c_str();
    }

    const npeErrorCode err_code = npeErrorCode::UNDEF;

   private:
    mutable std::string format_buf;
    std::string msg;
};

};  // namespace tt_npe

// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <fmt/core.h>

#include <magic_enum.hpp>
#include <string>
#include <variant>

#include "npeStats.hpp"

namespace tt_npe {

using BytesPerCycle = float;
using CycleCount = uint32_t;

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
        format_buf = fmt::format("{} - {}", magic_enum::enum_name(err_code), msg);
        return format_buf.c_str();
    }

    const npeErrorCode err_code = npeErrorCode::UNDEF;

   private:
    mutable std::string format_buf;
    std::string msg;
};

// empty result indicates failure
using npeResult = std::variant<npeException, npeStats>;
inline bool result_succeeded(const npeResult &result) {
    return std::holds_alternative<npeStats>(result);
}
inline std::optional<npeException> getErrorFromNPEResult(const npeResult &result) {
    if (result_succeeded(result)) {
        return {};
    } else {
        return std::get<npeException>(result);
    }
}
inline std::optional<npeStats> getStatsFromNPEResult(const npeResult &result) {
    if (result_succeeded(result)) {
        return std::get<npeStats>(result);
    } else {
        return {};
    }
}
};  // namespace tt_npe

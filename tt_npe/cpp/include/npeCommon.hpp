// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <fmt/core.h>

#include <magic_enum.hpp>
#include <string>

namespace tt_npe {

using BytesPerCycle = float;
using Cycle = uint64_t;
using Timestep = uint64_t;
using DeviceID = int16_t;
#define MESH_DEVICE -1

enum class nocType { NOC0 = 0, NOC1 = 1 };

// Selects how the bandwidth of a *single packet* (num_packets == 1) transfer is derived from the
// device's transfer bandwidth table. See interpolateBW() in npeDeviceModelUtils.hpp.
enum class SinglePacketBWModel : unsigned char {
    // Legacy behavior: the "first transfer" term of the blend gets weight 1.0, so a single packet
    // is always modelled at the table's peak bandwidth regardless of its size.
    Legacy = 0,
    // A single packet is modelled at the size-appropriate steady-state bandwidth from the table,
    // which below the table's knee is equivalent to a fixed per-transaction latency floor.
    LatencyFloor = 1,
};

enum class npeErrorCode {
    UNDEF = 0,
    WORKLOAD_VALIDATION_FAILED = 1,
    EXCEEDED_SIM_CYCLE_LIMIT = 2,
    INVALID_CONFIG = 3,
    DEVICE_MODEL_INIT_FAILED = 4,
    SIM_ENGINE_INIT_FAILED = 5,
    TRACE_INGEST_FAILED = 6,
    DEPENDENCY_GEN_FAILED = 7
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

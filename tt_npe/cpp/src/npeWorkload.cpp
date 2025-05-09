// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include "npeWorkload.hpp"

#include <filesystem>
#include <vector>

#include "fmt/core.h"
#include "npeDeviceModelIface.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

bool npeWorkloadTransfer::validate(
    const npeDeviceModel &device_model,
    const std::optional<std::filesystem::path> &source_file,
    bool verbose) const {
    bool valid_num_packets = num_packets > 0;
    bool valid_packet_size = packet_size > 0;
    auto is_valid_coord = [](const Coord &coord, size_t num_rows, size_t num_cols) {
        return (coord.row >= 0 && coord.row < num_rows) && (coord.col >= 0 && coord.col < num_cols);
    };

    // currently, all transfers are within a device; packets that span devices
    // tie together multiple single-device transfers using a TransferGroup
    bool src_and_dst_device_ids_match = src.device_id == getDeviceIDsFromNocDestination(dst).front();

    bool valid_src = is_valid_coord(src, device_model.getRows(), device_model.getCols());
    bool valid_dst = false;
    if (std::holds_alternative<Coord>(dst)) {
        const auto &dst_coord = std::get<Coord>(dst);
        valid_dst = is_valid_coord(dst_coord, device_model.getRows(), device_model.getCols());
    } else {
        const auto &dst_mcast = std::get<MulticastCoordSet>(dst);
        for (const auto &[start, end] : dst_mcast.coord_grids) {
            valid_dst = valid_dst || (is_valid_coord(start, device_model.getRows(), device_model.getCols()) &&
                                      is_valid_coord(end, device_model.getRows(), device_model.getCols()));
        }
    }

    bool valid_rel_start_time = phase_cycle_offset >= 0;

    bool valid = valid_num_packets && valid_packet_size && valid_src && valid_dst &&
                 valid_rel_start_time && src_and_dst_device_ids_match;

    if (!valid && verbose) {
        constexpr size_t msg_limit = 10;
        static size_t num_err_msgs = 0;
        if (num_err_msgs < msg_limit) {
            std::string source_name =
                source_file.has_value() ? source_file.value().filename().string() : "(generated)";
            log_error(
                "{} | Transfer #{:<3} is invalid : {}{}{}{}{}{}",
                source_name,
                this->getID(),
                (valid_num_packets) ? "" : "INVALID_NUM_PACKETS ",
                (valid_packet_size) ? "" : fmt::format("INVALID_PACKET_SIZE of {}", packet_size),
                (valid_src) ? "" : "INVALID_SRC ",
                (valid_dst) ? "" : "INVALID_DST ",
                (valid_rel_start_time) ? "" : "INVALID_REL_START_TIME ",
                (src_and_dst_device_ids_match) ? "" : "SRC_AND_DST_DEVICE_IDS_MISMATCH");
            num_err_msgs++;
        }
        if (num_err_msgs == msg_limit) {
            std::string source_name =
                source_file.has_value() ? source_file.value().filename().string() : "(generated)";
            log_error(
                "{} | Transfer #{:<3} is invalid : ... (limit reached)",
                source_name,
                this->getID());
            num_err_msgs++;
        }
    }

    return valid;
}

npeWorkloadPhaseID npeWorkload::addPhase(npeWorkloadPhase phase) {
    npeWorkloadPhaseID new_phase_id = phases.size();
    phase.id = new_phase_id;
    for (npeWorkloadTransfer &tr : phase.transfers) {
        tr.phase_id = new_phase_id;
        tr.id = gbl_transfer_id++;
    }
    phases.push_back(std::move(phase));
    return new_phase_id;
}

bool npeWorkload::validate(const npeDeviceModel &npe_device_model, bool verbose) const {
    // bitmaps for detecting id aliasing
    std::vector<bool> phase_id_bitmap(phases.size(), false);
    std::vector<bool> transfer_id_bitmap(gbl_transfer_id, false);

    size_t errors = 0;
    for (const auto &ph : phases) {
        if (ph.id > phases.size()) {
            if (verbose)
                log_error("WorkloadValidation | Phase {} has invalid (out-of-range) ID!", ph.id);
            errors++;
            continue;
        } else if (phase_id_bitmap[ph.id]) {
            if (verbose)
                log_error("WorkloadValidation | Phase {} has repeated ID!", ph.id);
            errors++;
            continue;
        } else {
            phase_id_bitmap[ph.id] = true;
        }

        for (const auto &tr : ph.transfers) {
            if (tr.id > gbl_transfer_id) {
                if (verbose)
                    log_error(
                        "WorkloadValidation | Transfer {} has invalid (out-of-range) ID!", tr.id);
                errors++;
                continue;
            } else if (transfer_id_bitmap[tr.id]) {
                if (verbose)
                    log_error("WorkloadValidation | Transfer {} has repeated ID!", tr.id);
                errors++;
                continue;
            } else {
                transfer_id_bitmap[tr.id] = true;
            }

            if (not tr.validate(
                    npe_device_model,
                    getSourceFilePath(),
                    verbose)) {
                errors++;
            }
        }
    }

    return errors == 0;
}

void npeWorkload::scaleWorkloadSchedule(float scale_factor) {
    uint32_t max_before = 0;
    uint32_t max_after = 0;
    for (auto &ph : phases) {
        for (auto &tr : ph.transfers) {
            max_before = std::max(max_before, tr.phase_cycle_offset);
            tr.phase_cycle_offset = static_cast<CycleCount>(tr.phase_cycle_offset * scale_factor);
            max_after = std::max(max_after, tr.phase_cycle_offset);
        }
    }
}

void npeWorkload::inferInjectionRates(const npeDeviceModel &device_model) {
    for (auto &ph : phases) {
        for (auto &tr : ph.transfers) {
            auto inferred_ir = device_model.getSrcInjectionRate(tr.src);
            // fmt::println("injection rate changed from {:.2f} -> {:.2f} ", tr.injection_rate,
            // inferred_ir);
            tr.injection_rate = inferred_ir;
        }
    }
}

npeWorkload npeWorkload::removeLocalUnicastTransfers() const {
    size_t removed_transfers = 0;
    size_t total_transfers = 0;
    uint64_t removed_bytes = 0;
    uint64_t total_bytes = 0;

    npeWorkload wl;
    wl.setGoldenResultCycles(getGoldenResultCycles());
    for (auto &ph : phases) {
        auto &transfers = ph.transfers;
        npeWorkloadPhase new_ph;
        for (const auto &tr : transfers) {
            if (std::holds_alternative<Coord>(tr.dst)) {
                const auto &dst_coord = std::get<Coord>(tr.dst);
                if ((tr.src.row / 2 == dst_coord.row / 2) &&
                    (tr.src.col / 2 == dst_coord.col / 2)) {
                    removed_transfers++;
                    removed_bytes += tr.total_bytes;
                } else {
                    new_ph.transfers.push_back(tr);
                }
                total_transfers++;
                total_bytes += tr.total_bytes;
            }
        }
        wl.addPhase(std::move(new_ph));
    }

    return wl;
}

}  // namespace tt_npe

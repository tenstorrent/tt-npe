#include "npeWorkload.hpp"

#include <vector>

#include "fmt/core.h"
#include "npeDeviceModel.hpp"
#include "util.hpp"

namespace tt_npe {

bool npeWorkloadTransfer::validate(
    size_t device_num_rows, size_t device_num_cols, bool verbose) const {
    bool valid_num_packets = num_packets > 0;
    bool valid_packet_size = packet_size > 0;
    bool valid_src =
        (src.row >= 0 && src.row < device_num_rows) && (src.col >= 0 && src.col < device_num_cols);
    bool valid_dst =
        (dst.row >= 0 && dst.row < device_num_rows) && (dst.col >= 0 && dst.col < device_num_cols);
    bool valid_rel_start_time = phase_cycle_offset >= 0;

    bool valid =
        valid_num_packets && valid_packet_size && valid_src && valid_dst && valid_rel_start_time;

    if (!valid && verbose) {
        log_error(
            "WorkloadValidation | Transfer #{:<3} is invalid : {}{}{}{}{}",
            this->getID(),
            (valid_num_packets) ? "" : "INVALID_NUM_PACKETS ",
            (valid_packet_size) ? "" : "INVALID_PACKET_SIZE ",
            (valid_src) ? "" : "INVALID_SRC ",
            (valid_dst) ? "" : "INVALID_DST ",
            (valid_rel_start_time) ? "" : "INVALID_REL_START_TIME ");
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
                    log_error("WorkloadValidation | Transfer {} has invalid (out-of-range) ID!", tr.id);
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

            if (not tr.validate(npe_device_model.getRows(), npe_device_model.getCols(), verbose)) {
                errors++;
            }
        }
    }

    return errors == 0;
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

}  // namespace tt_npe

#include "npeWorkload.hpp"

#include <vector>

#include "fmt/core.h"
#include "npeDeviceModel.hpp"
#include "util.hpp"

namespace tt_npe {

bool nocWorkloadTransfer::validate(size_t device_num_rows, size_t device_num_cols) const {
    bool valid_num_packets = num_packets > 0;
    bool valid_packet_size = packet_size > 0;
    bool valid_src = (src.row >= 0 && src.row < device_num_rows) && (src.col >= 0 && src.col < device_num_cols);
    bool valid_dst = (dst.row >= 0 && dst.row < device_num_rows) && (dst.col >= 0 && dst.col < device_num_cols);
    bool valid_rel_start_time = cycle_offset >= 0;

    return valid_num_packets && valid_packet_size && valid_src && valid_dst && valid_rel_start_time;
}

nocWorkloadPhaseID nocWorkload::addPhase(nocWorkloadPhase phase) {
    nocWorkloadPhaseID new_phase_id = phases.size();
    phase.id = new_phase_id;
    for (nocWorkloadTransfer &tr : phase.transfers) {
        tr.phase_id = new_phase_id;
        tr.id = gbl_transfer_id++;
    }
    phases.push_back(std::move(phase));
    return new_phase_id;
}

bool nocWorkload::validate(const nocModel &noc_model, bool verbose) const {
    // bitmaps for detecting id aliasing
    std::vector<bool> phase_id_bitmap(phases.size(), false);
    std::vector<bool> transfer_id_bitmap(gbl_transfer_id, false);

    size_t errors = 0;
    for (const auto &ph : phases) {
        if (ph.id > phases.size()) {
            if (verbose)
                error("Phase {} has invalid (out-of-range) ID!", ph.id);
            errors++;
            continue;
        } else if (phase_id_bitmap[ph.id]) {
            if (verbose)
                error("Phase {} has repeated ID!", ph.id);
            errors++;
            continue;
        } else {
            phase_id_bitmap[ph.id] = true;
        }

        for (const auto &tr : ph.transfers) {
            if (tr.id > gbl_transfer_id) {
                if (verbose)
                    error("Transfer {} has invalid (out-of-range) ID!", tr.id);
                errors++;
                continue;
            } else if (transfer_id_bitmap[tr.id]) {
                if (verbose)
                    error("Transfer {} has repeated ID!", tr.id);
                errors++;
                continue;
            } else {
                transfer_id_bitmap[tr.id] = true;
            }

            if (not tr.validate(noc_model.getRows(), noc_model.getCols())) {
                errors++;
            }
        }
    }

    return errors == 0;
}

}  // namespace tt_npe

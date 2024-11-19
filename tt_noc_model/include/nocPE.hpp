#pragma once

#include "nocModel.hpp"
#include "nocWorkload.hpp"

namespace tt_npe {

// returns various results from noc simulation
struct nocPEStats {
  size_t total_cycles;
};

class nocPE {
public:
  nocPE() = default;
  nocPE(const std::string &device_name) {
    // initialize noc model
    model = nocModel(device_name);
  }

  nocPEStats runPerfEstimation(const nocWorkload &wl) {

    Timestep t = 0;

    /*
    Determine all phases that have satisfied dependencies
    Insert all transfers from ready-to-run Phases into a sorted queue
    loop {

      Move all transfers from tr_queue that have start_time <= t to
    live_transfer_list

      Compute bandwidth for this timestep for all live transfers
        congestion model eventually goes here

      Update all live transfer state
        Live transfers can be in multiple states (AWAIT_RESP, TRANSMIT, ...)
        Track remaining bytes to transmit
        If all bytes transmitted, mark transfer as complete

      if new phase is unlocked:
        Add phase transfer's to tr_queue

      if num_live_transfers == 0 AND tr_queue.size() == 0 AND
        break

      Advance time step
    }
     */

    return nocPEStats{};
  }

private:
  nocModel model;
};

} // namespace tt_npe
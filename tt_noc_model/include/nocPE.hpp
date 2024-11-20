#pragma once

#include "fmt/base.h"
#include "nocModel.hpp"
#include "nocWorkload.hpp"
#include "util.hpp"

namespace tt_npe {

// returns various results from noc simulation
struct nocPEStats {
  size_t total_cycles;
};

struct Transfer {
  size_t total_bytes;
  size_t packet_size;
  Coord src, dst;
  float injection_rate; // how many GB/cycle the source can inject
  CycleCount start_cycle = 0;

  float curr_bandwidth = 0;
  size_t total_bytes_transferred = 0;

  bool operator<(const auto &rhs) const {
    return start_cycle < rhs.start_cycle;
  }
  bool operator>(const auto &rhs) const {
    return start_cycle > rhs.start_cycle;
  }
};

class nocPE {
public:
  nocPE() = default;
  nocPE(const std::string &device_name) {
    // initialize noc model
    model = nocModel(device_name);
  }

  void addPhaseTransfersToQueue(std::vector<Transfer> *transfer_queue,
                                const nocWorkloadPhase &ph,
                                CycleCount curr_cycles) {
    assert(transfer_queue);
    transfer_queue->reserve(transfer_queue->size() + ph.transfers.size());
    for (const auto &tx : ph.transfers) {
      // adjust start time to be
      auto adj_start_cycle = curr_cycles + tx.cycle_offset + (rand() % 20);
      transfer_queue->push_back(Transfer{.total_bytes = tx.bytes,
                                         .packet_size = tx.packet_size,
                                         .src = tx.src,
                                         .dst = tx.dst,
                                         .injection_rate = tx.injection_rate,
                                         .start_cycle = adj_start_cycle});
    }
  }

  void sortTransferQueue(std::vector<Transfer> *transfer_queue) {
    assert(transfer_queue);
    std::stable_sort(transfer_queue->begin(), transfer_queue->end(),
                     std::greater<Transfer>());
  }

  void updateTransferBandwidth(std::vector<Transfer> *live_transfers) {
    for (auto &lt : *live_transfers) {
      lt.curr_bandwidth = 28;
    }
  }

  nocPEStats runPerfEstimation(const nocWorkload &wl,
                               size_t cycles_per_timestep) {

    CycleCount curr_cycle = 0;
    nocPEStats stats;

    // TODO: Determine all phases that have satisfied dependencies
    auto ready_phases = wl.getPhases();

    // Insert all transfers from ready-to-run Phases into a sorted queue
    std::vector<Transfer> transfer_queue;
    for (const auto &ph : ready_phases) {
      addPhaseTransfersToQueue(&transfer_queue, ph, curr_cycle);
    }
    sortTransferQueue(&transfer_queue);

    // main simulation loop
    std::vector<Transfer> live_transfers;
    live_transfers.reserve(transfer_queue.size());
    while (true) {

      // transfer now-active transfers to live_transfers
      while (transfer_queue.size() &&
             transfer_queue.back().start_cycle <= curr_cycle) {
        live_transfers.push_back(transfer_queue.back());
        transfer_queue.pop_back();
      }

      // Compute bandwidth for this timestep for all live transfers
      // congestion model eventually goes here
      updateTransferBandwidth(&live_transfers);

      // Update all live transfer state
      float worst_case_transfer_end_cycle = 0;
      for (size_t i = 0; i < live_transfers.size(); i++) {
        auto &lt = live_transfers[i];

        size_t cycles_transferring =
            std::min(cycles_per_timestep, curr_cycle - lt.start_cycle);
        size_t bytes_transferred = cycles_transferring * lt.curr_bandwidth;

        //if (cycles_transferring < cycles_per_timestep) {
        //  fmt::println("Starting transfer for cycle {} at curr_cycle {}, "
        //               "cycles_transferring {}",
        //               lt.start_cycle, curr_cycle, cycles_transferring);
        //}

        // if phase almost complete, track worst case cycles to complete
        // transfers to get true end time
        size_t remaining_bytes = lt.total_bytes - lt.total_bytes_transferred;
        if (remaining_bytes <= bytes_transferred &&
            transfer_queue.size() == 0) {

          float cycles_transferring =
              std::fmin(bytes_transferred, remaining_bytes) /
              float(lt.curr_bandwidth);

          worst_case_transfer_end_cycle = std::max(
              worst_case_transfer_end_cycle,
              (curr_cycle - (cycles_per_timestep - cycles_transferring)));

          //if (curr_cycle > 2500)
          //  fmt::println(
          //      "transfer starting at {} ended early at {}", lt.start_cycle,
          //  worst_case_transfer_end_cycle);
        }

        lt.total_bytes_transferred =
            std::min(lt.total_bytes,
                     size_t(lt.total_bytes_transferred + bytes_transferred));
      }
      // fmt::println("{:3.2f} ({:3.3f}%) max cycles transferring",
      //              max_cycles_transferring,
      //              100.0 * max_cycles_transferring / cycles_per_timestep);

      // compact live transfer list, removing completed transfers
      auto transfer_complete = [](const Transfer &tr) {
        return tr.total_bytes_transferred == tr.total_bytes;
      };
      live_transfers.erase(std::remove_if(live_transfers.begin(),
                                          live_transfers.end(),
                                          transfer_complete),
                           live_transfers.end());
      // fmt::println("{} live transfers", live_transfers.size());

      // TODO: if new phase is unlocked, add phase transfer's to tr_queue

      // end sim loop if all transfers have been completed
      if (live_transfers.size() == 0 and transfer_queue.size() == 0) {
        // Assume on average that transfers end in the middle of the timestep
        stats.total_cycles = worst_case_transfer_end_cycle;
        break;
      }

      if (curr_cycle > MAX_CYCLE_LIMIT) {
        fmt::println("ERROR: exceeded max cycle limit!");
        break;
      }

      // Advance time step
      curr_cycle += cycles_per_timestep;
    }

    return stats;
  }

private:
  nocModel model;
  static constexpr size_t MAX_CYCLE_LIMIT = 100000;
};

} // namespace tt_npe
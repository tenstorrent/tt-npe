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
      lt.curr_bandwidth = 32;
    }
  }

  nocPEStats runPerfEstimation(const nocWorkload &wl, size_t cycles_per_timestep) {

    CycleCount curr_cycle = 0;

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
             transfer_queue.back().start_cycle < curr_cycle) {
        live_transfers.push_back(transfer_queue.back());
        transfer_queue.pop_back();
        //fmt::println("Cycle {:4d} : Start transfer to {} left={}", curr_cycle,
        //             live_transfers.back().dst, transfer_queue.size());
      }

      // Compute bandwidth for this timestep for all live transfers
      // congestion model eventually goes here
      updateTransferBandwidth(&live_transfers);

      // Update all live transfer state
      size_t next_free_slot = 0;
      for (size_t i = 0; i < live_transfers.size(); i++) {
        assert(next_free_slot <= i);

        auto &lt = live_transfers[i];
        auto progress = cycles_per_timestep * lt.curr_bandwidth;
        //fmt::println("{} progress",progress);
        lt.total_bytes_transferred = std::min(
            lt.total_bytes, size_t(lt.total_bytes_transferred + progress));
        bool transfer_complete = lt.total_bytes_transferred == lt.total_bytes;

        // move live transfers to replace completed ones
        if (not transfer_complete) {
          if (next_free_slot < i) {
            std::swap(live_transfers[next_free_slot], live_transfers[i]);
          }
          //fmt::println("Cycle {:4d} : Transfer to {} progress {}/{} ",
          //             curr_cycle, live_transfers[next_free_slot].dst,
          //             live_transfers[next_free_slot].total_bytes_transferred,
          //             live_transfers[next_free_slot].total_bytes);
          next_free_slot++;
        } else {
          //fmt::println("Cycle {:4d} : Completed transfer to {}", curr_cycle,
          //             live_transfers[i].dst);
        }
      }
      live_transfers.resize(next_free_slot);

      // TODO: if new phase is unlocked, add phase transfer's to tr_queue

      // Advance time step
      curr_cycle += cycles_per_timestep;

      // end sim loop if all transfers have been completed
      if (live_transfers.size() == 0 and transfer_queue.size() == 0) {
        //fmt::println("Simulation done at {} cycles", curr_cycle - (cycles_per_timestep/2));
        break;
      }

      if (curr_cycle > 80000) {
        fmt::println("ERROR: exceeded max cycle limit!");
        break;
      }
    }

    return nocPEStats{};
  }

private:
  nocModel model;
};

} // namespace tt_npe
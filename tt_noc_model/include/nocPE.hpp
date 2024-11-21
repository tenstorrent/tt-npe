#pragma once

#include "fmt/base.h"
#include "nocModel.hpp"
#include "nocWorkload.hpp"
#include "util.hpp"
#include <algorithm>

namespace tt_npe {

// returns various results from noc simulation
struct nocPEStats {
  size_t total_cycles;
  size_t simulated_cycles;
  size_t num_timesteps;
};

using PETransferID = int;
struct PETransfer {
  size_t total_bytes;
  size_t packet_size;
  Coord src, dst;
  float injection_rate; // how many GB/cycle the source can inject
  CycleCount cycle_offset;

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

  using BytesPerCycle = float;
  using TransferBandwidthTable = std::vector<std::pair<size_t, BytesPerCycle>>;

  float interpolateBW(const TransferBandwidthTable &tbt, size_t packet_size) {
    for (int fst = 0; fst < tbt.size() - 1; fst++) {
      size_t start_range = tbt[fst].first;
      size_t end_range = tbt[fst + 1].first;
      if (packet_size >= start_range && packet_size <= end_range) {
        float delta = end_range - start_range;
        float pct = (packet_size - start_range) / delta;
        float val_delta = tbt[fst + 1].second - tbt[fst].second;
        float interp_val = (val_delta * pct) + tbt[fst].second;
        // fmt::println("interp bw for ps={} is {:5.2f}", packet_size,
        // interp_val);
        return interp_val;
      }
    }
    assert(0);
  }

  void updateTransferBandwidth(std::vector<PETransfer> *transfers,
                               std::vector<PETransferID> *live_transfer_ids) {

    static const TransferBandwidthTable tbt = {
        {0, 0},       {128, 5.5},   {256, 10.1}, {512, 18.0},
        {1024, 27.4}, {2048, 28.1}, {8192, 28.1}};

    for (auto &ltid : *live_transfer_ids) {
      auto &lt = (*transfers)[ltid];
      auto noc_limited_bw = interpolateBW(tbt, lt.packet_size);
      lt.curr_bandwidth = std::min(lt.injection_rate, noc_limited_bw);
    }
  }

  nocPEStats runPerfEstimation(const nocWorkload &wl,
                               uint32_t cycles_per_timestep) {

    nocPEStats stats;

    if (not wl.validate(model)) {
      error("Failed to validate workload; see errors above.");
      return nocPEStats{};
    }

    // construct flat vector of all transfers from workload
    std::vector<PETransfer> transfers;
    size_t num_transfers = 0;
    for (const auto &ph : wl.getPhases()) {
      num_transfers += ph.transfers.size();
    }

    transfers.resize(num_transfers);
    for (const auto &ph : wl.getPhases()) {
      for (const auto &wl_transfer : ph.transfers) {
        assert(wl_transfer.id < num_transfers);
        transfers[wl_transfer.id] =
            PETransfer{.total_bytes = wl_transfer.bytes,
                       .packet_size = wl_transfer.packet_size,
                       .src = wl_transfer.src,
                       .dst = wl_transfer.dst,
                       .injection_rate = wl_transfer.injection_rate,
                       .cycle_offset = wl_transfer.cycle_offset};
      }
    }

    // TODO: Determine all phases that have satisfied dependencies
    auto ready_phases = wl.getPhases();

    // Insert start_cycle,transfer pairs into a sorted queue for dispatching in
    // main sim loop
    struct TPair {
      CycleCount start_cycle;
      PETransferID id;
    };
    std::vector<TPair> tq;
    for (const auto &ph : ready_phases) {
      for (const auto &tr : ph.transfers) {
        // start time is phase start (curr_cycle) + offset within phase
        // (cycle_offset)
        auto adj_start_cycle = tr.cycle_offset;
        transfers[tr.id].start_cycle = adj_start_cycle;

        tq.push_back({adj_start_cycle, tr.id});
      }
    }
    std::stable_sort(tq.begin(), tq.end(),
                     [](const TPair &lhs, const TPair &rhs) {
                       return std::make_pair(lhs.start_cycle, lhs.id) >
                              std::make_pair(rhs.start_cycle, rhs.id);
                     });

    // main simulation loop
    std::vector<PETransferID> live_transfer_ids;
    live_transfer_ids.reserve(transfers.size());
    size_t timestep = 0;
    CycleCount curr_cycle = cycles_per_timestep;
    while (true) {

      // fmt::println("\n---- curr cycle {} ------------------------------",
      //              curr_cycle);

      // transfer now-active transfers to live_transfers
      while (tq.size() && tq.back().start_cycle <= curr_cycle) {
        auto id = tq.back().id;
        live_transfer_ids.push_back(id);
        // fmt::println("Transfer {} START cycle {} size={}", id,
        //              transfers[id].start_cycle, transfers[id].total_bytes);
        tq.pop_back();
      }

      // Compute bandwidth for this timestep for all live transfers
      // congestion model eventually goes here
      updateTransferBandwidth(&transfers, &live_transfer_ids);

      // Update all live transfer state
      size_t worst_case_transfer_end_cycle = 0;
      for (auto ltid : live_transfer_ids) {
        auto &lt = transfers[ltid];

        size_t remaining_bytes = lt.total_bytes - lt.total_bytes_transferred;
        size_t cycles_active_in_curr_timestep =
            std::min(cycles_per_timestep, curr_cycle - lt.start_cycle);
        size_t bytes_transferred =
            std::min(remaining_bytes, size_t(cycles_active_in_curr_timestep *
                                             lt.curr_bandwidth));
        lt.total_bytes_transferred += bytes_transferred;

        // compute cycle where transfer ended
        if (lt.total_bytes == lt.total_bytes_transferred) {
          float cycles_transferring =
              std::ceil(bytes_transferred / float(lt.curr_bandwidth));
          size_t transfer_end_cycle =
              (curr_cycle - cycles_per_timestep) + cycles_transferring;

          // fmt::println("Transfer {} ended on cycle {}", ltid,
          //              transfer_end_cycle);
          worst_case_transfer_end_cycle =
              std::max(worst_case_transfer_end_cycle, transfer_end_cycle);
        }
      }

      // compact live transfer list, removing completed transfers
      auto transfer_complete = [&transfers](const PETransferID id) {
        return transfers[id].total_bytes_transferred ==
               transfers[id].total_bytes;
      };
      live_transfer_ids.erase(std::remove_if(live_transfer_ids.begin(),
                                             live_transfer_ids.end(),
                                             transfer_complete),
                              live_transfer_ids.end());

      // TODO: if new phase is unlocked, add phase transfer's to tr_queue

      // end sim loop if all transfers have been completed
      if (live_transfer_ids.size() == 0 and tq.size() == 0) {
        stats.total_cycles = worst_case_transfer_end_cycle;
        stats.num_timesteps = timestep + 1;
        stats.simulated_cycles = stats.num_timesteps * cycles_per_timestep;
        break;
      }

      if (curr_cycle > MAX_CYCLE_LIMIT) {
        fmt::println("ERROR: exceeded max cycle limit!");
        break;
      }

      // Advance time step
      curr_cycle += cycles_per_timestep;
      timestep++;
    }

    return stats;
  }
  const nocModel &getModel() { return model; }

private:
  nocModel model;
  static constexpr size_t MAX_CYCLE_LIMIT = 100000;
};

} // namespace tt_npe
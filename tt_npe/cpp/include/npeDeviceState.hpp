// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include "grid.hpp"
#include "npeDeviceTypes.hpp"

namespace tt_npe {

// Class to encapsulate device state information including NIU and link demand grids
class npeDeviceState {
public:
    // Constructor takes the number of NIUs (indexed by nocNIUID) and links for demand grids.
    // num_dram_slots is (num_chips * num_dram_controllers_per_chip); passing 0 (the default)
    // leaves the DRAM controller demand grid empty, which disables the DRAM congestion model.
    npeDeviceState(size_t num_nius, size_t num_links, size_t num_dram_slots = 0) :
        niu_demand_grid(num_nius, 0.0f),
        link_demand_grid(num_links, 0.0f),
        multicast_write_link_demand_grid(num_links, 0.0f),
        dram_demand_grid(num_dram_slots, 0.0f) {}

    // Reset all demand grids to zero
    void reset() {
        std::fill(niu_demand_grid.begin(), niu_demand_grid.end(), 0.0f);
        std::fill(link_demand_grid.begin(), link_demand_grid.end(), 0.0f);
        std::fill(
            multicast_write_link_demand_grid.begin(),
            multicast_write_link_demand_grid.end(),
            0.0f);
        std::fill(dram_demand_grid.begin(), dram_demand_grid.end(), 0.0f);
    }

    // Accessors
    NIUDemandGrid& getNIUDemandGrid() { return niu_demand_grid; }
    LinkDemandGrid& getLinkDemandGrid() { return link_demand_grid; }
    LinkDemandGrid& getMulticastWriteLinkDemandGrid() { return multicast_write_link_demand_grid; }
    DramDemandGrid& getDramDemandGrid() { return dram_demand_grid; }

    const NIUDemandGrid& getNIUDemandGrid() const { return niu_demand_grid; }
    const LinkDemandGrid& getLinkDemandGrid() const { return link_demand_grid; }
    const LinkDemandGrid& getMulticastWriteLinkDemandGrid() const {
        return multicast_write_link_demand_grid;
    }
    const DramDemandGrid& getDramDemandGrid() const { return dram_demand_grid; }

private:
    NIUDemandGrid niu_demand_grid;
    LinkDemandGrid link_demand_grid;
    LinkDemandGrid multicast_write_link_demand_grid;
    DramDemandGrid dram_demand_grid;
};

} // namespace tt_npe

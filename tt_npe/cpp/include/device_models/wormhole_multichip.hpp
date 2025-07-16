// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#pragma once

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include "device_models/wormhole_b0.hpp"
#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeDeviceTypes.hpp"

namespace tt_npe {

class WormholeMultichipDeviceModel : public npeDeviceModel {
   public:
    WormholeMultichipDeviceModel(size_t num_chips = 8) : _num_chips(num_chips) {
        for (size_t i = 0; i < getNumChips(); i++) {
            _device_ids.insert(i);
        }
        TT_ASSERT(_device_ids.size() == getNumChips());
        populateNoCLinkLookups();
        populateNoCNIULookups();
    }
    ~WormholeMultichipDeviceModel() override = default;

    void populateNoCLinkLookups() {
        for (size_t device_id = 0; device_id < _num_chips; device_id++) {
            for (size_t r = 0; r < getRows(); r++) {
                for (size_t c = 0; c < getCols(); c++) {
                    for (const auto &link_type : getLinkTypes()) {
                        nocLinkAttr attr = {{device_id, r, c}, link_type};
                        link_id_to_attr_lookup.push_back(attr);
                        link_attr_to_id_lookup[attr] = link_id_to_attr_lookup.size() - 1;
                    }
                }
            }
        }
    }

    void populateNoCNIULookups() {
        for (size_t device_id = 0; device_id < _num_chips; device_id++) {
            for (size_t r = 0; r < getRows(); r++) {
                for (size_t c = 0; c < getCols(); c++) {
                    for (const auto &niu_type : getNIUTypes()) {
                        nocNIUAttr attr = {{device_id, r, c}, niu_type};
                        niu_id_to_attr_lookup.push_back(attr);
                        niu_attr_to_id_lookup[attr] = niu_id_to_attr_lookup.size() - 1;
                    }
                }
            }
        }
    }

    // substitutes desired device_id into every link of input route 
    nocRoute changeRouteDeviceID(const nocRoute& route, DeviceID device_id) const { 
        nocRoute end_route;
        for (const auto& link_id : route) {
            nocLinkAttr dev0_attr = getLinkAttributes(link_id);
            // substitute device_id with startpoint.device_id
            nocLinkAttr new_attr = {{device_id, dev0_attr.coord.row, dev0_attr.coord.col}, dev0_attr.type};
            nocLinkID new_link_id = getLinkID(new_attr);
            end_route.push_back(new_link_id);
        }
        return end_route;
    }

    // returns link-by-link route from startpoint to destination(s) for the specified noc type
    nocRoute route(nocType noc_type, const Coord &startpoint, const NocDestination &destination)
        const override {
        // assert that this route is between two coords on the same device!
        auto destination_device_ids = getDeviceIDsFromNocDestination(destination);
        TT_ASSERT(destination_device_ids.size() == 1);  
        TT_ASSERT(destination_device_ids[0] == startpoint.device_id);  

        // route using wormhole_b0 model, then swap out device_id for correct one
        nocRoute dev0_route = _wormhole_b0_model.route(noc_type, startpoint, destination);
        return changeRouteDeviceID(dev0_route, startpoint.device_id);
    }

    // Initialize device state with appropriate dimensions for this device model
    std::unique_ptr<npeDeviceState> initDeviceState() const override {
        size_t num_niu_types = niu_id_to_attr_lookup.size();
        size_t num_links = link_id_to_attr_lookup.size();
        return std::make_unique<npeDeviceState>(num_niu_types, num_links);
    }

    void modelCongestion(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfers,
        const std::vector<PETransferID> &live_transfer_ids,
        NIUDemandGrid &niu_demand_grid,
        LinkDemandGrid &link_demand_grid) const {
        size_t cycles_per_timestep = end_timestep - start_timestep;

        // assume all links have identical bandwidth
        float LINK_BANDWIDTH = _wormhole_b0_model.getLinkBandwidth(nocLinkID());
        static auto worker_sink_absorption_rate =
            _wormhole_b0_model.getSinkAbsorptionRateByCoreType(CoreType::WORKER);

        // determine effective demand through each link
        std::fill(link_demand_grid.begin(), link_demand_grid.end(), 0.0f);
        std::fill(niu_demand_grid.begin(), niu_demand_grid.end(), 0.0f);
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfers[ltid];

            // account for transfers starting mid-way into timestep, and derate effective
            // utilization accordingly
            CycleCount predicted_start = std::max(start_timestep, lt.start_cycle);
            float effective_demand =
                float(end_timestep - predicted_start) / float(cycles_per_timestep);
            effective_demand *= lt.curr_bandwidth;

            // track demand at src and sink NIU
            nocNIUType src_niu_type =
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC;
            nocNIUID niu_id = getNIUID(
                lt.params.src.device_id, lt.params.src.row, lt.params.src.col, src_niu_type);
            niu_demand_grid[niu_id] += effective_demand;

            nocNIUType sink_niu_type =
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK : nocNIUType::NOC1_SINK;
            if (std::holds_alternative<Coord>(lt.params.dst)) {
                const auto &dst = std::get<Coord>(lt.params.dst);
                nocNIUID niu_id = getNIUID(dst.device_id, dst.row, dst.col, sink_niu_type);
                niu_demand_grid[niu_id] += effective_demand;
            } else {
                const auto &mcast_dst = std::get<MulticastCoordSet>(lt.params.dst);
                for (auto c : mcast_dst) {
                    // multicast only loads on WORKER NIUs; other NIUS ignore traffic
                    if (getCoreType(c) == CoreType::WORKER) {
                        nocNIUID niu_id = getNIUID(c.device_id, c.row, c.col, sink_niu_type);
                        niu_demand_grid[niu_id] += effective_demand;
                    }
                }
            }

            for (const auto &link_id : lt.route) {
                link_demand_grid[link_id] += effective_demand;
            }
        }

        // find highest demand resource on each route to set bandwidth
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfers[ltid];

            // find max link demand on route
            float max_link_demand_on_route = 0;
            auto update_max_link_demand = [&max_link_demand_on_route](float demand) -> bool {
                if (demand > max_link_demand_on_route) {
                    max_link_demand_on_route = demand;
                    return true;
                } else {
                    return false;
                }
            };
            for (const auto &link_id : lt.route) {
                update_max_link_demand(link_demand_grid[link_id]);
            }
            auto min_link_bw_derate = LINK_BANDWIDTH / max_link_demand_on_route;

            // compute bottleneck (min derate factor) for source and sink NIUs
            auto src_niu_type =
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC;
            auto src_bw_demand = niu_demand_grid[getNIUID(
                lt.params.src.device_id, lt.params.src.row, lt.params.src.col, src_niu_type)];
            auto src_bw_derate = lt.params.injection_rate / src_bw_demand;

            auto sink_niu_type =
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK : nocNIUType::NOC1_SINK;

            float sink_bw_derate = 1;
            if (std::holds_alternative<Coord>(lt.params.dst)) {
                const auto &dst = std::get<Coord>(lt.params.dst);
                auto sink_bw_demand =
                    niu_demand_grid[getNIUID(dst.device_id, dst.row, dst.col, sink_niu_type)];
                sink_bw_derate = getSinkAbsorptionRate(dst) / sink_bw_demand;
            } else {
                // multicast transfer speed is set by the slowest sink NIU
                const auto &mcast_dst = std::get<MulticastCoordSet>(lt.params.dst);
                float sink_demand = 0;
                for (const auto &loc : mcast_dst) {
                    if (getCoreType(loc) == CoreType::WORKER) {
                        sink_demand = std::min(
                            sink_demand,
                            niu_demand_grid[getNIUID(
                                loc.device_id, loc.row, loc.col, sink_niu_type)]);
                    }
                }
                sink_bw_derate = worker_sink_absorption_rate / sink_demand;
            }

            auto min_niu_bw_derate = std::min(src_bw_derate, sink_bw_derate);

            if (min_link_bw_derate < 1.0 || min_niu_bw_derate < 1.0) {
                float overall_bw_derate = std::min(min_link_bw_derate, min_niu_bw_derate);
                lt.curr_bandwidth *= overall_bw_derate;
            }
        }
    }

    // Compute current transfer rate using device state
    void computeCurrentTransferRate(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfer_state,
        const std::vector<PETransferID> &live_transfer_ids,
        npeDeviceState &device_state,
        TimestepStats &sim_stats,
        bool enable_congestion_model) const override {

        // Compute bandwidth for this timestep for all live transfers
        updateTransferBandwidth(
            &transfer_state,
            live_transfer_ids,
            _wormhole_b0_model.getTransferBandwidthTable(),
            _wormhole_b0_model.getMaxNoCTransferBandwidth());

        // model congestion and derate bandwidth
        if (enable_congestion_model) {
            modelCongestion(
                start_timestep,
                end_timestep,
                transfer_state,
                live_transfer_ids,
                device_state.getNIUDemandGrid(),
                device_state.getLinkDemandGrid());

            updateSimulationStats(
                *this,
                device_state.getLinkDemandGrid(),
                device_state.getNIUDemandGrid(),
                sim_stats,
                _wormhole_b0_model.getLinkBandwidth(nocLinkID(0)));
        }
    }

    // returns number of rows and columns
    size_t getRows() const override { return _wormhole_b0_model.getRows(); }
    size_t getCols() const override { return _wormhole_b0_model.getCols(); }
    size_t getNumChips() const override { return _num_chips; }
    const boost::unordered_flat_set<DeviceID> &getDeviceIDs() const override {
        return _device_ids;
    }
    bool isValidDeviceID(DeviceID device_id_arg) const override {
        return _device_ids.contains(device_id_arg);
    }

    //------ Link lookups -----------------------------------------------------
    const nocLinkAttr &getLinkAttributes(const nocLinkID &link_id) const override {
        TT_ASSERT(link_id < link_id_to_attr_lookup.size());
        return link_id_to_attr_lookup[link_id];
    }
    nocLinkID getLinkID(const nocLinkAttr &link_attr) const override {
        auto it = link_attr_to_id_lookup.find(link_attr);
        TT_ASSERT(
            it != link_attr_to_id_lookup.end(),
            "Could not find Link ID for nocLinkAttr {{ {}, {} }}",
            link_attr.coord,
            magic_enum::enum_name(link_attr.type));
        return it->second;
    }
    const std::vector<nocLinkType> &getLinkTypes() const override {
        return _wormhole_b0_model.getLinkTypes();
    }

    //------ NIU lookups -----------------------------------------------------
    const nocNIUAttr &getNIUAttributes(const nocNIUID &niu_id) const override {
        TT_ASSERT(niu_id < niu_id_to_attr_lookup.size(), "NIU ID {} is not valid", niu_id);
        return niu_id_to_attr_lookup[niu_id];
    }
    nocNIUID getNIUID(const nocNIUAttr &niu_attr) const override {
        auto it = niu_attr_to_id_lookup.find(niu_attr);
        TT_ASSERT(
            it != niu_attr_to_id_lookup.end(),
            "Could not find NIU ID for nocNIUAttr {{ {}, {} }}",
            niu_attr.coord,
            magic_enum::enum_name(niu_attr.type));
        return it->second;
    }
    nocNIUID getNIUID(DeviceID device_id, size_t row, size_t col, nocNIUType type) const {
        return getNIUID(nocNIUAttr{{device_id, row, col}, type});
    }
    const std::vector<nocNIUType> &getNIUTypes() const override {
        return _wormhole_b0_model.getNIUTypes();
    }

    //------ Core Info Lookups ------------------------------------------------
    // NOTE: Coordinates are sliced internally to discard device_id, so
    // we can directly reuse wormhole_b0 implementations.
    CoreType getCoreType(const Coord &c) const override {
        return _wormhole_b0_model.getCoreType(c);
    }
    BytesPerCycle getSrcInjectionRate(const Coord &c) const override {
        return _wormhole_b0_model.getSrcInjectionRate(c);
    }
    BytesPerCycle getSinkAbsorptionRate(const Coord &c) const override {
        return _wormhole_b0_model.getSinkAbsorptionRate(c);
    }

    float getAggregateDRAMBandwidth() const override {
        return getNumChips() * _wormhole_b0_model.getAggregateDRAMBandwidth();
    }
    
    DeviceArch getArch() const override { return _wormhole_b0_model.getArch(); }

   protected:
    WormholeB0DeviceModel _wormhole_b0_model;
    size_t _num_chips = 8;

    std::vector<nocLinkAttr> link_id_to_attr_lookup;
    boost::unordered_flat_map<nocLinkAttr, nocLinkID> link_attr_to_id_lookup;
    std::vector<nocNIUAttr> niu_id_to_attr_lookup;
    boost::unordered_flat_map<nocNIUAttr, nocNIUID> niu_attr_to_id_lookup;
    boost::unordered_flat_set<DeviceID> _device_ids; 
};

}  // namespace tt_npe

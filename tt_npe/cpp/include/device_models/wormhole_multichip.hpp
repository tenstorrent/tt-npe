// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#pragma once

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include "device_models/wormhole_b0.hpp"
#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"

namespace tt_npe {

class WormholeMultichipDeviceModel : public npeDeviceModel {
   public:
    WormholeMultichipDeviceModel(size_t num_chips = 8) : _num_chips(num_chips) {
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

    // returns unicast route from startpoint to endpoint for the specified noc type
    nocRoute unicastRoute(
        nocType noc_type, const Coord &startpoint, const Coord &endpoint) const override {
        return _wormhole_b0_model.unicastRoute(noc_type, startpoint, endpoint);
    }

    // returns link-by-link route from startpoint to destination(s) for the specified noc type
    nocRoute route(nocType noc_type, const Coord &startpoint, const NocDestination &destination)
        const override {
        return _wormhole_b0_model.route(noc_type, startpoint, destination);
    }

    // Initialize device state with appropriate dimensions for this device model
    std::unique_ptr<npeDeviceState> initDeviceState() const override {
        return _wormhole_b0_model.initDeviceState();
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
        // TODO: Implement transfer rate computation
    }

    // returns number of rows and columns
    size_t getRows() const override { return _wormhole_b0_model.getRows(); }
    size_t getCols() const override { return _wormhole_b0_model.getCols(); }
    size_t getNumChips() const override { return _num_chips; }

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

   protected:
    WormholeB0DeviceModel _wormhole_b0_model;
    size_t _num_chips = 8;

    std::vector<nocLinkAttr> link_id_to_attr_lookup;
    boost::unordered_flat_map<nocLinkAttr, nocLinkID> link_attr_to_id_lookup;
    std::vector<nocNIUAttr> niu_id_to_attr_lookup;
    boost::unordered_flat_map<nocNIUAttr, nocNIUID> niu_attr_to_id_lookup;
};

}  // namespace tt_npe

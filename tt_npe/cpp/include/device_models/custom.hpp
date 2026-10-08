// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#pragma once

#include <array>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include "grid.hpp"
#include "npeDeviceModelConfig.hpp"
#include "npeDeviceModelIface.hpp"

namespace tt_npe {

class CustomDeviceModel final : public npeDeviceModel {
   public:
    explicit CustomDeviceModel(
        ResolvedNpeDeviceModelConfig resolved_config, size_t num_chips = 1);
    CustomDeviceModel(
        ResolvedNpeDeviceModelConfig resolved_config,
        boost::unordered_flat_set<DeviceID> device_ids);
    ~CustomDeviceModel() override = default;

    nocRoute route(
        nocIndex noc,
        const Coord& startpoint,
        const NocDestination& destination) const override;

    std::unique_ptr<npeDeviceState> initDeviceState() const override;

    void computeCurrentTransferRate(
        Cycle start_timestep,
        Cycle end_timestep,
        std::vector<PETransferState>& transfer_state,
        const std::vector<PETransferID>& live_transfer_ids,
        npeDeviceState& device_state,
        bool enable_congestion_model) const override;

    Cycle getReadLatency(const Coord& source, const Coord& destination) const override;
    Cycle getWriteLatency(
        const Coord& source, const Coord& destination, nocIndex noc) const override;

    DeviceArch getArch() const override;

    size_t getRows() const override;
    size_t getCols() const override;
    size_t getNumChips() const override;
    const boost::unordered_flat_set<DeviceID>& getDeviceIDs() const override;
    bool isValidDeviceID(DeviceID device_id) const override;

    const nocLinkAttr& getLinkAttributes(const nocLinkID& link_id) const override;
    nocLinkID getLinkID(const nocLinkAttr& link_attr) const override;
    const std::vector<nocLinkKind>& getLinkKinds() const override;
    size_t getNumNocs() const override;
    size_t getNumLinksPerChip(nocIndex noc) const override;

    const nocNIUAttr& getNIUAttributes(const nocNIUID& niu_id) const override;
    nocNIUID getNIUID(const nocNIUAttr& niu_attr) const override;
    const std::vector<nocNIUKind>& getNIUKinds() const override;

    CoreType getCoreType(const Coord& coord) const override;
    uint32_t getDramControllerIDForCore(const Coord& coord) const override;
    BytesPerCycle getSrcInjectionRate(const Coord& coord) const override;
    BytesPerCycle getSinkAbsorptionRate(const Coord& coord) const override;

    float getLinkBandwidth(const nocLinkID& link_id) const override;
    float getDRAMBandwidthPerChip() const override;
    float getDRAMBandwidthPerController() const override;
    float getEthBandwidthPerLink() const override;

   private:
    void populateCoreLookups();
    void populateNoCLookups();
    void modelCongestion(
        Cycle start_timestep,
        Cycle end_timestep,
        std::vector<PETransferState>& transfers,
        const std::vector<PETransferID>& live_transfer_ids,
        NIUDemandGrid& niu_demand_grid,
        LinkDemandGrid& link_demand_grid,
        LinkDemandGrid& multicast_write_link_demand_grid) const;
    bool linkExists(size_t row, size_t col, nocLinkType type) const;
    nocRoute unicastRoute(
        nocIndex noc, const Coord& startpoint, const Coord& destination) const;
    nocRoute torusUnicastRoute(
        nocIndex noc, const Coord& startpoint, const Coord& destination) const;
    nocRoute meshXYUnicastRoute(
        nocIndex noc, const Coord& startpoint, const Coord& destination) const;
    nocRoute torusMulticastRoute(
        nocIndex noc,
        const Coord& startpoint,
        const MulticastCoordSet::CoordGrid& grid) const;
    nocRoute meshXYMulticastRoute(
        nocIndex noc, const Coord& startpoint, const MulticastCoordSet::CoordGrid& grid) const;

    const NpeNocConfig& nocConfig() const { return resolved_config_.model_config.noc; }

    ResolvedNpeDeviceModelConfig resolved_config_;
    DeviceArch arch_;
    size_t num_chips_;
    Grid2D<CoreType> core_types_;
    DramCoordToControllerMapping dram_controller_by_coord_;
    boost::unordered_flat_set<DeviceID> device_ids_;

    std::vector<nocLinkAttr> link_attributes_by_id_;
    boost::unordered_flat_map<nocLinkAttr, nocLinkID> link_id_by_attributes_;
    std::vector<nocNIUAttr> niu_attributes_by_id_;
    boost::unordered_flat_map<nocNIUAttr, nocNIUID> niu_id_by_attributes_;

    std::vector<nocLinkKind> link_kinds_;
    std::vector<nocNIUKind> niu_kinds_;
    std::array<size_t, MAX_NOCS> num_links_per_chip_by_noc_ = {};
};

}  // namespace tt_npe

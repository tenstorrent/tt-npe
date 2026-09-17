// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#include "device_models/custom.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>
#include <utility>

#include "npeDeviceModelUtils.hpp"

namespace tt_npe {
namespace {

size_t checkedGridDimension(int dimension, std::string_view name) {
    if (dimension <= 0 ||
        dimension > static_cast<int>(std::numeric_limits<int16_t>::max())) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format("CustomDeviceModel has invalid grid {} {}", name, dimension));
    }
    return static_cast<size_t>(dimension);
}

std::pair<size_t, size_t> parsePhysicalCoord(
    std::string_view value, const std::filesystem::path& soc_descriptor_path) {
    const auto separator = value.find('-');
    if (separator == std::string_view::npos) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Invalid coordinate '{}' in SOC descriptor '{}'",
                value,
                soc_descriptor_path.string()));
    }

    int col = -1;
    int row = -1;
    const auto col_result =
        std::from_chars(value.data(), value.data() + separator, col);
    const auto row_result = std::from_chars(
        value.data() + separator + 1, value.data() + value.size(), row);
    if (col_result.ec != std::errc() ||
        col_result.ptr != value.data() + separator ||
        row_result.ec != std::errc() ||
        row_result.ptr != value.data() + value.size() || row < 0 || col < 0) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "Invalid coordinate '{}' in SOC descriptor '{}'",
                value,
                soc_descriptor_path.string()));
    }
    return {static_cast<size_t>(row), static_cast<size_t>(col)};
}

boost::unordered_flat_set<DeviceID> makeContiguousDeviceIDs(size_t num_chips) {
    if (num_chips == 0 ||
        num_chips >
            static_cast<size_t>(std::numeric_limits<DeviceID>::max()) + 1) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            "CustomDeviceModel chip count exceeds the DeviceID range");
    }

    boost::unordered_flat_set<DeviceID> device_ids;
    for (size_t device_id = 0; device_id < num_chips; ++device_id) {
        device_ids.insert(static_cast<DeviceID>(device_id));
    }
    return device_ids;
}

}  // namespace

CustomDeviceModel::CustomDeviceModel(
    ResolvedNpeDeviceModelConfig resolved_config, size_t num_chips) :
    CustomDeviceModel(
        std::move(resolved_config), makeContiguousDeviceIDs(num_chips)) {}

CustomDeviceModel::CustomDeviceModel(
    ResolvedNpeDeviceModelConfig resolved_config,
    boost::unordered_flat_set<DeviceID> device_ids) :
    resolved_config_(std::move(resolved_config)),
    num_chips_(device_ids.size()),
    core_types_(
        checkedGridDimension(resolved_config_.soc_descriptor.grid_y_size, "height"),
        checkedGridDimension(resolved_config_.soc_descriptor.grid_x_size, "width"),
        CoreType::UNDEF),
    device_ids_(std::move(device_ids)) {
    if (normalizeDeviceArchName(resolved_config_.soc_descriptor.arch_name) != "blackhole") {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format(
                "CustomDeviceModel currently supports only Blackhole, not '{}'",
                resolved_config_.soc_descriptor.arch_name));
    }
    if (num_chips_ == 0) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            "CustomDeviceModel requires at least one chip");
    }

    for (const auto device_id : device_ids_) {
        if (device_id < 0) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                "CustomDeviceModel device IDs must be non-negative");
        }
    }

    populateCoreLookups();
    populateNoCLookups();
}

void CustomDeviceModel::populateCoreLookups() {
    const auto& soc = resolved_config_.soc_descriptor;
    const auto& soc_path = resolved_config_.soc_descriptor_path;

    auto assign_core_type = [this, &soc_path](std::string_view value, CoreType type) {
        const auto [row, col] = parsePhysicalCoord(value, soc_path);
        if (!core_types_.inBounds(row, col)) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Coordinate '{}' is outside the {}x{} SOC grid in '{}'",
                    value,
                    getCols(),
                    getRows(),
                    soc_path.string()));
        }
        auto& current_type = core_types_(row, col);
        if (current_type != CoreType::UNDEF && current_type != type) {
            throw npeException(
                npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                fmt::format(
                    "Coordinate '{}' has conflicting core types in SOC descriptor '{}'",
                    value,
                    soc_path.string()));
        }
        current_type = type;
        return std::pair{row, col};
    };

    for (const auto& coord : soc.functional_workers) {
        assign_core_type(coord, CoreType::WORKER);
    }
    for (size_t controller_id = 0; controller_id < soc.dram.size(); ++controller_id) {
        for (const auto& coord : soc.dram[controller_id]) {
            const auto [row, col] = assign_core_type(coord, CoreType::DRAM);
            const Coord physical_coord{0, static_cast<int16_t>(row), static_cast<int16_t>(col)};
            if (dram_controller_by_coord_.contains(physical_coord)) {
                throw npeException(
                    npeErrorCode::DEVICE_MODEL_INIT_FAILED,
                    fmt::format(
                        "DRAM coordinate '{}' appears in multiple controllers in '{}'",
                        coord,
                        soc_path.string()));
            }
            dram_controller_by_coord_[physical_coord] = controller_id;
        }
    }
    for (const auto& coord : soc.eth) {
        assign_core_type(coord, CoreType::ETH);
    }
}

void CustomDeviceModel::populateNoCLookups() {
    const auto resource_count =
        num_chips_ * getRows() * getCols() * link_types_.size();
    if (resource_count >
        static_cast<size_t>(std::numeric_limits<nocLinkID>::max()) + 1) {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            "CustomDeviceModel link count exceeds the nocLinkID range");
    }

    link_attributes_by_id_.reserve(resource_count);
    niu_attributes_by_id_.reserve(
        num_chips_ * getRows() * getCols() * niu_types_.size());
    for (size_t device_index = 0; device_index < num_chips_; ++device_index) {
        const auto device_id = static_cast<DeviceID>(device_index);
        for (size_t row = 0; row < getRows(); ++row) {
            for (size_t col = 0; col < getCols(); ++col) {
                for (const auto type : link_types_) {
                    const nocLinkAttr attributes{
                        {device_id, static_cast<int16_t>(row), static_cast<int16_t>(col)}, type};
                    const auto id = static_cast<nocLinkID>(link_attributes_by_id_.size());
                    link_attributes_by_id_.push_back(attributes);
                    link_id_by_attributes_[attributes] = id;
                }
                for (const auto type : niu_types_) {
                    const nocNIUAttr attributes{
                        {device_id, static_cast<int16_t>(row), static_cast<int16_t>(col)}, type};
                    const auto id = static_cast<nocNIUID>(niu_attributes_by_id_.size());
                    niu_attributes_by_id_.push_back(attributes);
                    niu_id_by_attributes_[attributes] = id;
                }
            }
        }
    }
}

nocRoute CustomDeviceModel::unicastRoute(
    nocType noc_type, const Coord& startpoint, const Coord& destination) const {
    TT_ASSERT(startpoint.device_id == destination.device_id);
    TT_ASSERT(isValidDeviceID(startpoint.device_id));

    int64_t row = startpoint.row;
    int64_t col = startpoint.col;
    nocRoute route;
    if (noc_type == nocType::NOC0) {
        while (col != destination.col) {
            route.push_back(getLinkID(
                {{startpoint.device_id, static_cast<int16_t>(row), static_cast<int16_t>(col)},
                 nocLinkType::NOC0_EAST}));
            col = wrapToRange(col + 1, getCols());
        }
        while (row != destination.row) {
            route.push_back(getLinkID(
                {{startpoint.device_id, static_cast<int16_t>(row), static_cast<int16_t>(col)},
                 nocLinkType::NOC0_SOUTH}));
            row = wrapToRange(row + 1, getRows());
        }
    } else {
        while (row != destination.row) {
            route.push_back(getLinkID(
                {{startpoint.device_id, static_cast<int16_t>(row), static_cast<int16_t>(col)},
                 nocLinkType::NOC1_NORTH}));
            row = wrapToRange(row - 1, getRows());
        }
        while (col != destination.col) {
            route.push_back(getLinkID(
                {{startpoint.device_id, static_cast<int16_t>(row), static_cast<int16_t>(col)},
                 nocLinkType::NOC1_WEST}));
            col = wrapToRange(col - 1, getCols());
        }
    }
    return route;
}

nocRoute CustomDeviceModel::route(
    nocType noc_type,
    const Coord& startpoint,
    const NocDestination& destination) const {
    if (std::holds_alternative<Coord>(destination)) {
        return unicastRoute(noc_type, startpoint, std::get<Coord>(destination));
    }

    const auto& multicast = std::get<MulticastCoordSet>(destination);
    TT_ASSERT(multicast.coord_grids.size() == 1);
    const auto& grid = multicast.coord_grids.front();
    boost::unordered_flat_set<nocLinkID> unique_links;
    if (noc_type == nocType::NOC0) {
        for (int col = grid.start_coord.col; col <= grid.end_coord.col; ++col) {
            const auto partial_route = unicastRoute(
                noc_type,
                startpoint,
                {startpoint.device_id, grid.end_coord.row, static_cast<int16_t>(col)});
            unique_links.insert(partial_route.begin(), partial_route.end());
        }
    } else {
        for (int row = grid.start_coord.row; row <= grid.end_coord.row; ++row) {
            const auto partial_route = unicastRoute(
                noc_type,
                startpoint,
                {startpoint.device_id, static_cast<int16_t>(row), grid.end_coord.col});
            unique_links.insert(partial_route.begin(), partial_route.end());
        }
    }
    return {unique_links.begin(), unique_links.end()};
}

std::unique_ptr<npeDeviceState> CustomDeviceModel::initDeviceState() const {
    return std::make_unique<npeDeviceState>(
        niu_attributes_by_id_.size(), link_attributes_by_id_.size());
}

void CustomDeviceModel::computeCurrentTransferRate(
    Cycle,
    Cycle,
    std::vector<PETransferState>& transfer_state,
    const std::vector<PETransferID>& live_transfer_ids,
    npeDeviceState&,
    bool enable_congestion_model) const {
    if (enable_congestion_model) {
        throw npeException(
            npeErrorCode::INVALID_CONFIG,
            "Congestion modeling is not implemented for CustomDeviceModel yet");
    }

    const auto& table = resolved_config_.model_config.transfer_bandwidth_table;
    const auto max_bandwidth =
        std::max_element(
            table.begin(), table.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.second < rhs.second;
            })
            ->second;
    updateTransferBandwidth(
        &transfer_state, live_transfer_ids, table, max_bandwidth);
}

Cycle CustomDeviceModel::getReadLatency(
    const Coord& source, const Coord& destination) const {
    TT_ASSERT(source.device_id == destination.device_id);
    const auto& latencies = resolved_config_.model_config.read_latencies;
    if (source.row == destination.row && source.col == destination.col) {
        return latencies.same_tile;
    }
    if (source.col == destination.col) {
        return latencies.same_col;
    }
    if (source.row == destination.row) {
        return latencies.same_row;
    }
    return latencies.diagonal;
}

Cycle CustomDeviceModel::getWriteLatency(
    const Coord& source, const Coord& destination, nocType noc_type) const {
    TT_ASSERT(source.device_id == destination.device_id);
    const auto& latencies = resolved_config_.model_config.write_latencies;
    size_t hops = 0;
    if (noc_type == nocType::NOC0) {
        hops += modulo(destination.col - source.col, static_cast<int>(getCols()));
        hops += modulo(destination.row - source.row, static_cast<int>(getRows()));
    } else {
        hops += modulo(source.col - destination.col, static_cast<int>(getCols()));
        hops += modulo(source.row - destination.row, static_cast<int>(getRows()));
    }
    return latencies.startup + (hops * latencies.cycles_per_hop);
}

DeviceArch CustomDeviceModel::getArch() const { return DeviceArch::Blackhole; }

size_t CustomDeviceModel::getRows() const {
    return static_cast<size_t>(resolved_config_.soc_descriptor.grid_y_size);
}

size_t CustomDeviceModel::getCols() const {
    return static_cast<size_t>(resolved_config_.soc_descriptor.grid_x_size);
}

size_t CustomDeviceModel::getNumChips() const { return num_chips_; }

const boost::unordered_flat_set<DeviceID>& CustomDeviceModel::getDeviceIDs() const {
    return device_ids_;
}

bool CustomDeviceModel::isValidDeviceID(DeviceID device_id) const {
    return device_ids_.contains(device_id);
}

const nocLinkAttr& CustomDeviceModel::getLinkAttributes(
    const nocLinkID& link_id) const {
    TT_ASSERT(link_id >= 0 && static_cast<size_t>(link_id) < link_attributes_by_id_.size());
    return link_attributes_by_id_[link_id];
}

nocLinkID CustomDeviceModel::getLinkID(const nocLinkAttr& link_attr) const {
    const auto iterator = link_id_by_attributes_.find(link_attr);
    TT_ASSERT(iterator != link_id_by_attributes_.end());
    return iterator->second;
}

const std::vector<nocLinkType>& CustomDeviceModel::getLinkTypes() const {
    return link_types_;
}

const nocNIUAttr& CustomDeviceModel::getNIUAttributes(const nocNIUID& niu_id) const {
    TT_ASSERT(niu_id >= 0 && static_cast<size_t>(niu_id) < niu_attributes_by_id_.size());
    return niu_attributes_by_id_[niu_id];
}

nocNIUID CustomDeviceModel::getNIUID(const nocNIUAttr& niu_attr) const {
    const auto iterator = niu_id_by_attributes_.find(niu_attr);
    TT_ASSERT(iterator != niu_id_by_attributes_.end());
    return iterator->second;
}

const std::vector<nocNIUType>& CustomDeviceModel::getNIUTypes() const {
    return niu_types_;
}

CoreType CustomDeviceModel::getCoreType(const Coord& coord) const {
    TT_ASSERT(isValidDeviceID(coord.device_id));
    TT_ASSERT(coord.row >= 0 && coord.col >= 0);
    TT_ASSERT(core_types_.inBounds(coord.row, coord.col));
    return core_types_(coord.row, coord.col);
}

uint32_t CustomDeviceModel::getDramControllerIDForCore(const Coord& coord) const {
    const Coord chip_zero_coord{0, coord.row, coord.col};
    const auto iterator = dram_controller_by_coord_.find(chip_zero_coord);
    TT_ASSERT(iterator != dram_controller_by_coord_.end());
    return iterator->second;
}

BytesPerCycle CustomDeviceModel::getSrcInjectionRate(const Coord& coord) const {
    return resolved_config_.model_config.injection_rates.at(getCoreType(coord));
}

BytesPerCycle CustomDeviceModel::getSinkAbsorptionRate(const Coord& coord) const {
    return resolved_config_.model_config.absorption_rates.at(getCoreType(coord));
}

float CustomDeviceModel::getLinkBandwidth(const nocLinkID&) const {
    return resolved_config_.model_config.link_bandwidth;
}

float CustomDeviceModel::getDRAMBandwidthPerController() const {
    const auto dram_injection =
        resolved_config_.model_config.injection_rates.at(CoreType::DRAM);
    const auto dram_absorption =
        resolved_config_.model_config.absorption_rates.at(CoreType::DRAM);
    return resolved_config_.model_config.dram_channels_per_controller *
           ((dram_injection + dram_absorption) / 2);
}

float CustomDeviceModel::getDRAMBandwidthPerChip() const {
    return resolved_config_.soc_descriptor.dram.size() *
           getDRAMBandwidthPerController();
}

float CustomDeviceModel::getEthBandwidthPerLink() const {
    return resolved_config_.model_config.eth_bandwidth_per_link;
}

}  // namespace tt_npe

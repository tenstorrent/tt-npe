// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once
#include <cassert>
#include <tuple>
#include <vector>

#include "npeUtil.hpp"

namespace tt_npe {

enum class nocNIUType {
    SRC = 0,
    SINK = 1,
};
// link direction; paired with a nocIndex to identify a link
enum class nocLinkType {
    NORTH = 0,
    WEST = 1,
    EAST = 2,
    SOUTH = 3,
};

// a kind of link or NIU present at every tile of a device
struct nocLinkKind {
    nocIndex noc;
    nocLinkType type;
};
struct nocNIUKind {
    nocIndex noc;
    nocNIUType type;
};

// note: all coords here are physical, NOT logical!

using nocLinkID = int16_t;
using nocRoute = std::vector<nocLinkID>;

struct nocLinkAttr {
    Coord coord;
    nocIndex noc;
    nocLinkType type;
    bool operator==(const auto& rhs) const {
        return noc == rhs.noc && type == rhs.type && coord == rhs.coord;
    }
};

using nocNIUID = int16_t;

struct nocNIUAttr {
    Coord coord;
    nocIndex noc;
    nocNIUType type;
    bool operator==(const auto& rhs) const {
        return noc == rhs.noc && type == rhs.type && coord == rhs.coord;
    }
};

using CoordToCoreTypeMapping = boost::unordered_flat_map<Coord, CoreType>;
using DramCoordToControllerMapping = boost::unordered_flat_map<Coord, uint32_t>;
using CoreTypeToInjectionRate = boost::unordered_flat_map<CoreType, BytesPerCycle>;
using CoreTypeToAbsorptionRate = boost::unordered_flat_map<CoreType, BytesPerCycle>;
using TransferBandwidthTable = std::vector<std::pair<size_t, BytesPerCycle>>;

}  // namespace tt_npe

namespace std {
template <>
struct hash<tt_npe::nocLinkAttr> {
    size_t operator()(const tt_npe::nocLinkAttr& attr) const {
        // noc and type share one hash_combine; these lookups sit in the congestion hot loop
        size_t seed = 0x0000000000000000;
        seed = tt_npe::hash_combine(seed, attr.coord);
        seed = tt_npe::hash_combine(
            seed, (static_cast<uint32_t>(attr.noc) << 16) | static_cast<uint32_t>(attr.type));
        return seed;
    }
};
template <>
struct hash<tt_npe::nocNIUAttr> {
    size_t operator()(const tt_npe::nocNIUAttr& attr) const {
        size_t seed = 0x0000000000000000;
        seed = tt_npe::hash_combine(seed, attr.coord);
        seed = tt_npe::hash_combine(
            seed, (static_cast<uint32_t>(attr.noc) << 16) | static_cast<uint32_t>(attr.type));
        return seed;
    }
};
}  // namespace std

namespace tt_npe {
inline std::size_t hash_value(tt_npe::nocLinkAttr const& attr) {
    return std::hash<tt_npe::nocLinkAttr>{}(attr);
}

inline std::size_t hash_value(tt_npe::nocNIUAttr const& attr) {
    return std::hash<tt_npe::nocNIUAttr>{}(attr);
}
}  // namespace tt_npe

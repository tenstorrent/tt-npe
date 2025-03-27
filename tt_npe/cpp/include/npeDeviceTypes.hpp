// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once
#include <cassert>
#include <vector>

#include "npeUtil.hpp"

namespace tt_npe {

enum class nocNIUType {
    NOC0_SRC = 0,
    NOC0_SINK = 1,
    NOC1_SRC = 2,
    NOC1_SINK = 3,
    MIMIR_SRC = 6,
    MIMIR_SINK = 7,
};
enum class nocLinkType {
    NOC1_NORTH = 0,
    NOC1_WEST = 1,
    NOC0_EAST = 2,
    NOC0_SOUTH = 3,
};

// note: all coords here are physical, NOT logical!

using nocLinkID = int16_t;
using nocRoute = std::vector<nocLinkID>;

struct nocLinkAttr {
    Coord coord;
    nocLinkType type;
    bool operator==(const auto& rhs) const {
        return std::make_pair(coord, type) == std::make_pair(rhs.coord, rhs.type);
    }
};

using nocNIUID = int16_t;

struct nocNIUAttr {
    Coord coord;
    nocNIUType type;
    bool operator==(const auto& rhs) const {
        return std::make_pair(coord, type) == std::make_pair(rhs.coord, rhs.type);
    }
};

}  // namespace tt_npe

namespace std {
template <>
struct hash<tt_npe::nocLinkAttr> {
    size_t operator()(const tt_npe::nocLinkAttr& attr) const {
        size_t seed = 0x0000000000000000;
        seed = tt_npe::hash_combine(seed, attr.coord);
        seed = tt_npe::hash_combine(seed, static_cast<uint32_t>(attr.type));
        return seed;
    }
};
template <>
struct hash<tt_npe::nocNIUAttr> {
    size_t operator()(const tt_npe::nocNIUAttr& attr) const {
        size_t seed = 0x0000000000000000;
        seed = tt_npe::hash_combine(seed, attr.coord);
        seed = tt_npe::hash_combine(seed, static_cast<uint32_t>(attr.type));
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

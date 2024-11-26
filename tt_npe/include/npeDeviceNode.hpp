#pragma once
#include <cassert>
#include <vector>

#include "util.hpp"

namespace tt_npe {

enum class nocLinkType { NOC1_NORTH = 0, NOC1_WEST = 1, NOC0_EAST = 2, NOC0_SOUTH = 3, NUM_LINK_TYPES = 4 };

struct nocLink {
    Coord src_coord;
    Coord dst_coord;
};

struct nocLinkID {
    Coord coord;
    nocLinkType type;
    bool operator==(const auto& rhs) const { return std::make_pair(coord, type) == std::make_pair(coord, type); }
};

struct nocNode {
    nocLink& getLink(nocLinkType link_type) {
        assert(size_t(link_type) < links.size());
        return links[size_t(link_type)];
    }
    std::vector<nocLink> links;
    Coord coord;
};

}  // namespace tt_npe

namespace std {
template <>
struct hash<tt_npe::nocLinkID> {
    size_t operator()(const tt_npe::nocLinkID& id) const {
        size_t hash = 0xBAADF00DBAADF00D;
        hash ^= id.coord.row + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= id.coord.col + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= int(id.type) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};
}  // namespace std

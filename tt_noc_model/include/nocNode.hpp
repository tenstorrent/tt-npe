#pragma once
#include <cassert>
#include <vector>

#include "magic_enum.hpp"
#include "util.hpp"

namespace tt_npe {

enum class nocLinkType {
  NOC1_NORTH = 0,
  NOC1_WEST = 1,
  NOC0_EAST = 2,
  NOC0_SOUTH = 3
};

struct nocLink {
  Coord src_coord;
  Coord dst_coord;
};

struct nocNode {
  nocLink &getLink(nocLinkType link_type) {
    assert(size_t(link_type) < links.size());
    return links[size_t(link_type)];
  }
  std::vector<nocLink> links;
  Coord coord;
};

} // namespace tt_npe
#pragma once

#include <fmt/printf.h>
#include <string>

#include "grid.hpp"
#include "nocNode.hpp"
#include "util.hpp"

namespace tt_npe {

using namespace magic_enum;

class nocModel {
public:
  nocModel() {}
  nocModel(const std::string &device_name) {
    if (device_name == "test") {
      buildTestDevice();
    }
  }

  // build small device for proof of concept
  void buildTestDevice() {
    const size_t xdim = 10;
    const size_t ydim = 12;
    noc_grid = Grid2D<nocNode>(xdim, ydim);

    // gen noc links between nocNode
    for (int x = 0; x < xdim; x++) {
      for (int y = 0; y < ydim; y++) {

        // assign coordinates within node
        auto &src_node = noc_grid(x, y);
        src_node.coord = {x, y};

        src_node.links.resize(4);

        // build NOC0 links
        src_node.getLink(nocLinkType::NOC0_EAST) = {
            .src_coord = {x, y}, .dst_coord = wrapNoCCoord({x + 1, y})};
        src_node.getLink(nocLinkType::NOC0_SOUTH) = {
            .src_coord = {x, y}, .dst_coord = wrapNoCCoord({x, y - 1})};

        // build NOC1
        src_node.getLink(nocLinkType::NOC1_WEST) = {
            .src_coord = {x, y}, .dst_coord = wrapNoCCoord({x - 1, y})};
        src_node.getLink(nocLinkType::NOC1_NORTH) = {
            .src_coord = {x, y}, .dst_coord = wrapNoCCoord({x, y + 1})};
      }
    }

    // for (nocNode &node : noc_grid) {
    //   fmt::println("\nNODE {}", node.coord);
    //   for (nocLinkType link_type : enum_values<nocLinkType>()) {
    //     fmt::println("  LINK {:10s} : {} -> {}", enum_name(link_type),
    //                  node.getLink(link_type).src_coord,
    //                  node.getLink(link_type).dst_coord);
    //   }
    // }
  }

  size_t getRows() const { return noc_grid.getRows(); }
  size_t getCols() const { return noc_grid.getCols(); }

  Coord wrapNoCCoord(const Coord &c) const {
    auto wrapped_x = mapToRange(c.x, getRows());
    auto wrapped_y = mapToRange(c.y, getCols());
    return Coord{wrapped_x, wrapped_y};
  }

private:
  std::string device_name;
  Grid2D<nocNode> noc_grid;
};

} // namespace tt_npe
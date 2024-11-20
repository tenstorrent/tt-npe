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
    const size_t kcols = 10;
    const size_t krows = 12;
    _noc_grid = Grid2D<nocNode>(krows, kcols);

    // gen noc links between nocNode
    for (int row = 0; row < krows; row++) {
      for (int col = 0; col < kcols; col++) {

        // assign coordinates within node
        auto &src_node = _noc_grid(row, col);
        src_node.coord = {row, col};

        src_node.links.resize(4);

        // build NOC0 links
        src_node.getLink(nocLinkType::NOC0_EAST) = {
            .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row + 1, col})};
        src_node.getLink(nocLinkType::NOC0_SOUTH) = {
            .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row, col - 1})};

        // build NOC1
        src_node.getLink(nocLinkType::NOC1_WEST) = {
            .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row - 1, col})};
        src_node.getLink(nocLinkType::NOC1_NORTH) = {
            .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row, col + 1})};
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

  size_t getRows() const { return _noc_grid.getRows(); }
  size_t getCols() const { return _noc_grid.getCols(); }

  Coord wrapNoCCoord(const Coord &c) const {
    auto wrapped_rows = mapToRange(c.row, getRows());
    auto wrapped_cols = mapToRange(c.col, getCols());
    return Coord{wrapped_rows, wrapped_cols};
  }

private:
  std::string _device_name;
  Grid2D<nocNode> _noc_grid;
};

} // namespace tt_npe
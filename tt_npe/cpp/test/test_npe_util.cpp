// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "npeUtil.hpp"

namespace tt_npe {

TEST(npeUtilTest, CanGetMCastCoordPairGridSize) {
    EXPECT_EQ(MCastCoordPair({1, 1}, {1, 1}).grid_size(), 1);
    EXPECT_EQ(MCastCoordPair({5, 5}, {5, 5}).grid_size(), 1);
    EXPECT_EQ(MCastCoordPair({1, 1}, {1, 2}).grid_size(), 2);
    EXPECT_EQ(MCastCoordPair({1, 1}, {4, 4}).grid_size(), 16);
}
TEST(npeUtilTest, CanIterateOverMCastCoordPair) {
    {
        MCastCoordPair mcp({1, 1}, {2, 2});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 4);
        std::vector<Coord> ref_results = {Coord{1, 1}, Coord{1, 2}, Coord{2, 1}, Coord{2, 2}};
        EXPECT_TRUE(coords == ref_results);
    }
    {
        MCastCoordPair mcp({1, 1}, {1, 1});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 1);
        std::vector<Coord> ref_results = {Coord{1, 1}};
        EXPECT_TRUE(coords == ref_results);
    }
    {
        MCastCoordPair mcp({1, 1}, {3, 3});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 9);
        std::vector<Coord> ref_results = {
            Coord{1, 1},
            Coord{1, 2},
            Coord{1, 3},
            Coord{2, 1},
            Coord{2, 2},
            Coord{2, 3},
            Coord{3, 1},
            Coord{3, 2},
            Coord{3, 3}};
        EXPECT_TRUE(coords == ref_results);
    }
    {
        MCastCoordPair mcp({3, 2}, {4, 7});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 12);
        std::vector<Coord> ref_results = {
            Coord{3, 2},
            Coord{3, 3},
            Coord{3, 4},
            Coord{3, 5},
            Coord{3, 6},
            Coord{3, 7},
            Coord{4, 2},
            Coord{4, 3},
            Coord{4, 4},
            Coord{4, 5},
            Coord{4, 6},
            Coord{4, 7}};
        EXPECT_TRUE(coords == ref_results);
    }
}

}  // namespace tt_npe

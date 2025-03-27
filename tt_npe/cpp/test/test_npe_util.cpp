// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "device_models/wormhole_b0.hpp"
#include "gtest/gtest.h"
#include "npeUtil.hpp"

namespace tt_npe {

TEST(npeUtilTest, CanGetMulticastCoordSetGridSize) {
    WormholeB0DeviceModel model;
    EXPECT_EQ(
        MulticastCoordSet({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 1, 1}).grid_size(), 1);
    EXPECT_EQ(
        MulticastCoordSet({model.getDeviceID(), 5, 5}, {model.getDeviceID(), 5, 5}).grid_size(), 1);
    EXPECT_EQ(
        MulticastCoordSet({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 1, 2}).grid_size(), 2);
    EXPECT_EQ(
        MulticastCoordSet({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 4, 4}).grid_size(),
        16);
}
TEST(npeUtilTest, CanIterateOverMulticastCoordSet) {
    WormholeB0DeviceModel model;
    {
        MulticastCoordSet mcp({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 2, 2});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 4);
        std::vector<Coord> ref_results = {
            Coord{model.getDeviceID(), 1, 1},
            Coord{model.getDeviceID(), 1, 2},
            Coord{model.getDeviceID(), 2, 1},
            Coord{model.getDeviceID(), 2, 2}};
        EXPECT_TRUE(coords == ref_results);
    }
    {
        MulticastCoordSet mcp({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 1, 1});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 1);
        std::vector<Coord> ref_results = {Coord{model.getDeviceID(), 1, 1}};
        EXPECT_TRUE(coords == ref_results);
    }
    {
        MulticastCoordSet mcp({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 3, 3});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 9);
        std::vector<Coord> ref_results = {
            Coord{model.getDeviceID(), 1, 1},
            Coord{model.getDeviceID(), 1, 2},
            Coord{model.getDeviceID(), 1, 3},
            Coord{model.getDeviceID(), 2, 1},
            Coord{model.getDeviceID(), 2, 2},
            Coord{model.getDeviceID(), 2, 3},
            Coord{model.getDeviceID(), 3, 1},
            Coord{model.getDeviceID(), 3, 2},
            Coord{model.getDeviceID(), 3, 3}};
        EXPECT_TRUE(coords == ref_results);
    }
    {
        MulticastCoordSet mcp({model.getDeviceID(), 3, 2}, {model.getDeviceID(), 4, 7});
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }
        EXPECT_EQ(coords.size(), 12);
        std::vector<Coord> ref_results = {
            Coord{model.getDeviceID(), 3, 2},
            Coord{model.getDeviceID(), 3, 3},
            Coord{model.getDeviceID(), 3, 4},
            Coord{model.getDeviceID(), 3, 5},
            Coord{model.getDeviceID(), 3, 6},
            Coord{model.getDeviceID(), 3, 7},
            Coord{model.getDeviceID(), 4, 2},
            Coord{model.getDeviceID(), 4, 3},
            Coord{model.getDeviceID(), 4, 4},
            Coord{model.getDeviceID(), 4, 5},
            Coord{model.getDeviceID(), 4, 6},
            Coord{model.getDeviceID(), 4, 7}};
        EXPECT_TRUE(coords == ref_results);
    }
}

TEST(npeUtilTest, MultiDeviceMulticastCoordSet) {
    // Create device models with different device IDs
    WormholeB0DeviceModel model0;  // Default device ID (0)

    class TestDeviceModel : public WormholeB0DeviceModel {
       public:
        TestDeviceModel(DeviceID id) : _test_device_id(id) {}
        DeviceID getDeviceID() const override { return _test_device_id; }

       private:
        DeviceID _test_device_id;
    };

    TestDeviceModel model1(DeviceID(1));
    TestDeviceModel model2(DeviceID(2));

    // Test MulticastCoordSet with device ID 0
    {
        MulticastCoordSet mcp({model0.getDeviceID(), 1, 1}, {model0.getDeviceID(), 2, 2});

        // Verify grid size
        EXPECT_EQ(mcp.grid_size(), 4);

        // Verify all coordinates have the correct device ID
        for (const auto& c : mcp) {
            EXPECT_EQ(c.device_id, model0.getDeviceID());
        }

        // Verify coordinates
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }

        std::vector<Coord> expected = {
            Coord{model0.getDeviceID(), 1, 1},
            Coord{model0.getDeviceID(), 1, 2},
            Coord{model0.getDeviceID(), 2, 1},
            Coord{model0.getDeviceID(), 2, 2}};

        EXPECT_EQ(coords, expected);

        // Verify string representation
        std::string str = fmt::format("{}", mcp);
        EXPECT_EQ(str, "Dev0(1,1)-(2,2)");
    }

    // Test MulticastCoordSet with device ID 1
    {
        MulticastCoordSet mcp({model1.getDeviceID(), 3, 3}, {model1.getDeviceID(), 4, 4});

        // Verify grid size
        EXPECT_EQ(mcp.grid_size(), 4);

        // Verify all coordinates have the correct device ID
        for (const auto& c : mcp) {
            EXPECT_EQ(c.device_id, model1.getDeviceID());
        }

        // Verify coordinates
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }

        std::vector<Coord> expected = {
            Coord{model1.getDeviceID(), 3, 3},
            Coord{model1.getDeviceID(), 3, 4},
            Coord{model1.getDeviceID(), 4, 3},
            Coord{model1.getDeviceID(), 4, 4}};

        EXPECT_EQ(coords, expected);

        // Verify string representation
        std::string str = fmt::format("{}", mcp);
        EXPECT_EQ(str, "Dev1(3,3)-(4,4)");
    }

    // Test MulticastCoordSet with device ID 2 and a different grid shape
    {
        MulticastCoordSet mcp({model2.getDeviceID(), 5, 5}, {model2.getDeviceID(), 5, 7});

        // Verify grid size
        EXPECT_EQ(mcp.grid_size(), 3);

        // Verify all coordinates have the correct device ID
        for (const auto& c : mcp) {
            EXPECT_EQ(c.device_id, model2.getDeviceID());
        }

        // Verify coordinates
        std::vector<Coord> coords;
        for (const auto& c : mcp) {
            coords.push_back(c);
        }

        std::vector<Coord> expected = {
            Coord{model2.getDeviceID(), 5, 5},
            Coord{model2.getDeviceID(), 5, 6},
            Coord{model2.getDeviceID(), 5, 7}};

        EXPECT_EQ(coords, expected);

        // Verify string representation
        std::string str = fmt::format("{}", mcp);
        EXPECT_EQ(str, "Dev2(5,5)-(5,7)");
    }
}

TEST(npeUtilTest, MultiDeviceDisjointMulticastCoordSet) {
    // Create device models with different device IDs
    WormholeB0DeviceModel model0;  // Default device ID (0)

    class TestDeviceModel : public WormholeB0DeviceModel {
       public:
        TestDeviceModel(DeviceID id) : _test_device_id(id) {}
        DeviceID getDeviceID() const override { return _test_device_id; }

       private:
        DeviceID _test_device_id;
    };

    TestDeviceModel model1(DeviceID(1));
    TestDeviceModel model2(DeviceID(2));

    // Create a MulticastCoordSet with multiple disjoint grids on different devices
    MulticastCoordSet::CoordGridContainer grids;

    // Add a grid on device 0
    grids.push_back(MulticastCoordSet::CoordGrid{
        Coord{model0.getDeviceID(), 1, 1}, Coord{model0.getDeviceID(), 2, 2}});

    // Add a grid on device 1
    grids.push_back(MulticastCoordSet::CoordGrid{
        Coord{model1.getDeviceID(), 3, 3}, Coord{model1.getDeviceID(), 4, 4}});

    // Add a grid on device 2
    grids.push_back(MulticastCoordSet::CoordGrid{
        Coord{model2.getDeviceID(), 5, 5}, Coord{model2.getDeviceID(), 5, 7}});

    MulticastCoordSet mcp(grids);

    // Verify grid size (should be sum of all grid sizes)
    EXPECT_EQ(mcp.grid_size(), 4 + 4 + 3);  // 4 coords in first grid, 4 in second, 3 in third

    // Verify coordinates by iterating through the MulticastCoordSet
    std::vector<Coord> coords;
    for (const auto& c : mcp) {
        coords.push_back(c);
    }

    // Expected coordinates in iteration order
    std::vector<Coord> expected = {// Device 0 grid
                                   Coord{model0.getDeviceID(), 1, 1},
                                   Coord{model0.getDeviceID(), 1, 2},
                                   Coord{model0.getDeviceID(), 2, 1},
                                   Coord{model0.getDeviceID(), 2, 2},
                                   // Device 1 grid
                                   Coord{model1.getDeviceID(), 3, 3},
                                   Coord{model1.getDeviceID(), 3, 4},
                                   Coord{model1.getDeviceID(), 4, 3},
                                   Coord{model1.getDeviceID(), 4, 4},
                                   // Device 2 grid
                                   Coord{model2.getDeviceID(), 5, 5},
                                   Coord{model2.getDeviceID(), 5, 6},
                                   Coord{model2.getDeviceID(), 5, 7}};

    EXPECT_EQ(coords, expected);

    // Verify string representation
    std::string str = fmt::format("{}", mcp);
    EXPECT_EQ(str, "Dev0(1,1)-(2,2), Dev1(3,3)-(4,4), Dev2(5,5)-(5,7)");

    // Test that we can access specific device grids
    bool found_dev0 = false;
    bool found_dev1 = false;
    bool found_dev2 = false;

    for (const auto& grid : mcp.coord_grids) {
        if (grid.start_coord.device_id == model0.getDeviceID()) {
            found_dev0 = true;
            EXPECT_EQ(grid.start_coord.row, 1);
            EXPECT_EQ(grid.start_coord.col, 1);
            EXPECT_EQ(grid.end_coord.row, 2);
            EXPECT_EQ(grid.end_coord.col, 2);
        } else if (grid.start_coord.device_id == model1.getDeviceID()) {
            found_dev1 = true;
            EXPECT_EQ(grid.start_coord.row, 3);
            EXPECT_EQ(grid.start_coord.col, 3);
            EXPECT_EQ(grid.end_coord.row, 4);
            EXPECT_EQ(grid.end_coord.col, 4);
        } else if (grid.start_coord.device_id == model2.getDeviceID()) {
            found_dev2 = true;
            EXPECT_EQ(grid.start_coord.row, 5);
            EXPECT_EQ(grid.start_coord.col, 5);
            EXPECT_EQ(grid.end_coord.row, 5);
            EXPECT_EQ(grid.end_coord.col, 7);
        }
    }

    EXPECT_TRUE(found_dev0);
    EXPECT_TRUE(found_dev1);
    EXPECT_TRUE(found_dev2);
}

}  // namespace tt_npe

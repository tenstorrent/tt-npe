// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "device_models/wormhole_b0.hpp"
#include "gtest/gtest.h"
#include "npeUtil.hpp"
#include <random>   // For random number generation

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

TEST(npeUtilTest, HashFunctions) {
    // Test hash_mix function
    size_t seed = 0xBAADF00DBAADF00D;
    size_t value1 = 42;
    size_t value2 = 123;
    
    // Mixing the same values in the same order should produce the same hash
    size_t hash1 = tt_npe::hash_mix(seed, value1);
    hash1 = tt_npe::hash_mix(hash1, value2);
    
    size_t hash2 = tt_npe::hash_mix(seed, value1);
    hash2 = tt_npe::hash_mix(hash2, value2);
    
    EXPECT_EQ(hash1, hash2);
    
    // Mixing values in different order should produce different hashes
    size_t hash3 = tt_npe::hash_mix(seed, value2);
    hash3 = tt_npe::hash_mix(hash3, value1);
    
    EXPECT_NE(hash1, hash3);
    
    // Test hash_container function
    std::vector<int> container1 = {1, 2, 3, 4, 5};
    std::vector<int> container2 = {1, 2, 3, 4, 5};
    std::vector<int> container3 = {5, 4, 3, 2, 1};
    
    size_t container_hash1 = tt_npe::hash_container(seed, container1);
    size_t container_hash2 = tt_npe::hash_container(seed, container2);
    size_t container_hash3 = tt_npe::hash_container(seed, container3);
    
    // Same container contents should produce the same hash
    EXPECT_EQ(container_hash1, container_hash2);
    
    // Different order of elements should produce different hash
    EXPECT_NE(container_hash1, container_hash3);
    
    // Test with MulticastCoordSet
    WormholeB0DeviceModel model;
    MulticastCoordSet mcs1({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 2, 2});
    MulticastCoordSet mcs2({model.getDeviceID(), 1, 1}, {model.getDeviceID(), 2, 2});
    MulticastCoordSet mcs3({model.getDeviceID(), 3, 3}, {model.getDeviceID(), 4, 4});
    
    // Same MulticastCoordSet should have the same hash
    EXPECT_EQ(std::hash<MulticastCoordSet>{}(mcs1), std::hash<MulticastCoordSet>{}(mcs2));
    
    // Different MulticastCoordSet should have different hash
    EXPECT_NE(std::hash<MulticastCoordSet>{}(mcs1), std::hash<MulticastCoordSet>{}(mcs3));
}

TEST(npeUtilTest, HashFunctionQuality) {
    // Test the quality of hash_mix and hash_container functions
    // by checking distribution of hash values for randomly initialized Coord objects
    
    const size_t NUM_SAMPLES = 10000;
    const size_t NUM_BUCKETS = 100;
    const double EXPECTED_BUCKET_SIZE = static_cast<double>(NUM_SAMPLES) / NUM_BUCKETS;
    const double MAX_DEVIATION = 0.3; // Allow up to 30% deviation from expected bucket size
    
    // Create a vector of random Coord objects
    std::vector<Coord> random_coords;
    random_coords.reserve(NUM_SAMPLES);
    
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> device_id_dist(0, 100);
    std::uniform_int_distribution<> row_dist(0, 1000);
    std::uniform_int_distribution<> col_dist(0, 1000);
    
    // Generate random Coord objects
    for (size_t i = 0; i < NUM_SAMPLES; i++) {
        DeviceID device_id = device_id_dist(gen);
        int16_t row = row_dist(gen);
        int16_t col = col_dist(gen);
        random_coords.emplace_back(device_id, row, col);
    }
    
    // Hash each Coord and count occurrences in buckets
    std::vector<size_t> bucket_counts(NUM_BUCKETS, 0);
    size_t seed = 0xBAADF00DBAADF00D;
    
    // Test hash_mix function with Coord components
    for (const auto& coord : random_coords) {
        // Mix the components of the Coord
        size_t hash_value = seed;
        hash_value = tt_npe::hash_mix(hash_value, coord.device_id);
        hash_value = tt_npe::hash_mix(hash_value, coord.row);
        hash_value = tt_npe::hash_mix(hash_value, coord.col);
        
        size_t bucket = hash_value % NUM_BUCKETS;
        bucket_counts[bucket]++;
    }
    
    // Check distribution quality for hash_mix
    size_t min_count = *std::min_element(bucket_counts.begin(), bucket_counts.end());
    size_t max_count = *std::max_element(bucket_counts.begin(), bucket_counts.end());
    
    // Calculate standard deviation
    double sum = 0.0;
    for (size_t count : bucket_counts) {
        double diff = count - EXPECTED_BUCKET_SIZE;
        sum += diff * diff;
    }
    double variance = sum / NUM_BUCKETS;
    double std_dev = std::sqrt(variance);
    double normalized_std_dev = std_dev / EXPECTED_BUCKET_SIZE;
    
    // Log the distribution statistics
    std::cout << "Hash mix distribution statistics for Coord components:" << std::endl;
    std::cout << "  Min bucket count: " << min_count << std::endl;
    std::cout << "  Max bucket count: " << max_count << std::endl;
    std::cout << "  Expected bucket size: " << EXPECTED_BUCKET_SIZE << std::endl;
    std::cout << "  Standard deviation: " << std_dev << std::endl;
    std::cout << "  Normalized std dev: " << normalized_std_dev << std::endl;
    
    // Verify the distribution is reasonably uniform
    EXPECT_LT(normalized_std_dev, MAX_DEVIATION) 
        << "hash_mix function has poor distribution quality for Coord components";
    
    // Reset bucket counts for hash_container test
    std::fill(bucket_counts.begin(), bucket_counts.end(), 0);
    
    // Test hash_container function with chunks of Coord objects
    const size_t CHUNK_SIZE = 5;
    size_t num_chunks = NUM_SAMPLES / CHUNK_SIZE;
    
    for (size_t i = 0; i < num_chunks; i++) {
        std::vector<Coord> chunk;
        for (size_t j = 0; j < CHUNK_SIZE && (i * CHUNK_SIZE + j) < random_coords.size(); j++) {
            chunk.push_back(random_coords[i * CHUNK_SIZE + j]);
        }
        
        size_t hash_value = tt_npe::hash_container(seed, chunk);
        size_t bucket = hash_value % NUM_BUCKETS;
        bucket_counts[bucket]++;
    }
    
    // Check distribution quality for hash_container
    min_count = *std::min_element(bucket_counts.begin(), bucket_counts.end());
    max_count = *std::max_element(bucket_counts.begin(), bucket_counts.end());
    
    // Recalculate expected bucket size for chunks
    const double EXPECTED_CHUNK_BUCKET_SIZE = static_cast<double>(num_chunks) / NUM_BUCKETS;
    
    // Calculate standard deviation for chunks
    sum = 0.0;
    for (size_t count : bucket_counts) {
        double diff = count - EXPECTED_CHUNK_BUCKET_SIZE;
        sum += diff * diff;
    }
    variance = sum / NUM_BUCKETS;
    std_dev = std::sqrt(variance);
    normalized_std_dev = std_dev / EXPECTED_CHUNK_BUCKET_SIZE;
    
    // Log the distribution statistics for chunks
    std::cout << "\nHash container distribution statistics for Coord chunks:" << std::endl;
    std::cout << "  Min bucket count: " << min_count << std::endl;
    std::cout << "  Max bucket count: " << max_count << std::endl;
    std::cout << "  Expected bucket size: " << EXPECTED_CHUNK_BUCKET_SIZE << std::endl;
    std::cout << "  Standard deviation: " << std_dev << std::endl;
    std::cout << "  Normalized std dev: " << normalized_std_dev << std::endl;
    
    // Verify the distribution is reasonably uniform for chunks
    EXPECT_LT(normalized_std_dev, MAX_DEVIATION) 
        << "hash_container function has poor distribution quality for Coord chunks";
    
    // Test for collisions in Coord objects
    std::unordered_set<size_t> unique_hashes;
    for (size_t i = 0; i < 1000 && i < random_coords.size(); i++) {
        size_t hash_value = std::hash<Coord>{}(random_coords[i]);
        unique_hashes.insert(hash_value);
    }
    
    // A good hash function should have minimal collisions for random Coords
    double collision_rate = 1.0 - (static_cast<double>(unique_hashes.size()) / std::min(static_cast<size_t>(1000), random_coords.size()));
    std::cout << "\nCollision rate for Coord objects: " << (collision_rate * 100.0) << "%" << std::endl;
    
    // Expect collision rate to be very low (less than 1%)
    EXPECT_LT(collision_rate, 0.01) << "std::hash<Coord> has high collision rate for random Coord objects";
}

}  // namespace tt_npe

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
    // Test hash_combine function
    size_t seed = 0;
    size_t value1 = 42;
    size_t value2 = 123;
    
    // Mixing the same values in the same order should produce the same hash
    size_t hash1 = tt_npe::hash_combine(seed, value1);
    hash1 = tt_npe::hash_combine(hash1, value2);
    
    size_t hash2 = tt_npe::hash_combine(seed, value1);
    hash2 = tt_npe::hash_combine(hash2, value2);
    
    EXPECT_EQ(hash1, hash2);
    
    // Mixing values in different order should produce different hashes
    size_t hash3 = tt_npe::hash_combine(seed, value2);
    hash3 = tt_npe::hash_combine(hash3, value1);
    
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
    
    // Define bounds for device_id, row, and col
    const int MAX_DEVICE_ID = 10;
    const int16_t MAX_ROW = 30;
    const int16_t MAX_COL = 30;

    const size_t NUM_SAMPLES = 10*30*30;
    const size_t NUM_BUCKETS = 107;
    const double EXPECTED_BUCKET_SIZE = static_cast<double>(NUM_SAMPLES) / NUM_BUCKETS;
    const double MAX_DEVIATION = 0.3; // Allow up to 30% deviation from expected bucket size
    
    std::random_device rd;
    std::mt19937 gen(rd());

    // Create a vector of random Coord objects
    boost::unordered_flat_set<Coord> random_coords;
    random_coords.reserve(NUM_SAMPLES);

    // Generate all possible Coord objects within the defined bounds
    for (int device_id = 0; device_id <= MAX_DEVICE_ID; ++device_id) {
        for (int16_t row = 0; row <= MAX_ROW; ++row) {
            for (int16_t col = 0; col <= MAX_COL; ++col) {
                random_coords.insert(Coord(DeviceID(device_id), row, col));
            }
        }
    }
    
    // Hash each Coord and count occurrences in buckets
    std::vector<size_t> bucket_counts(NUM_BUCKETS, 0);
    
    // Test hash function with Coord components
    for (const Coord& coord : random_coords) {
        // Use the hash_value function for Coord
        size_t hash_value = std::hash<tt_npe::Coord>{}(coord);
        size_t bucket = hash_value % NUM_BUCKETS;
        bucket_counts[bucket]++;
    }
    
    // Check distribution quality for hash function
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
    // std::cout << "Hash mix distribution statistics for Coord components:" << std::endl;
    // std::cout << "  Min bucket count: " << min_count << std::endl;
    // std::cout << "  Max bucket count: " << max_count << std::endl;
    // std::cout << "  Expected bucket size: " << EXPECTED_BUCKET_SIZE << std::endl;
    // std::cout << "  Standard deviation: " << std_dev << std::endl;
    // std::cout << "  Normalized std dev: " << normalized_std_dev << std::endl;
    
    // Verify the distribution is reasonably uniform
    EXPECT_LT(normalized_std_dev, MAX_DEVIATION) 
        << "hash_mix function has poor distribution quality for Coord components";
    
    // Reset bucket counts for hash_container test
    std::fill(bucket_counts.begin(), bucket_counts.end(), 0);
    
}

TEST(npeUtilTest, HashContainerBasic) {
    // Test that hash_container produces consistent results for the same input
    // and different results for different inputs
    
    // Test with vectors of integers
    std::vector<int> vec1 = {1, 2, 3, 4, 5};
    std::vector<int> vec2 = {1, 2, 3, 4, 5}; // Same as vec1
    std::vector<int> vec3 = {5, 4, 3, 2, 1}; // Reversed order
    std::vector<int> vec4 = {1, 2, 3, 4, 6}; // One element different
    
    // Compute hashes
    size_t hash1 = tt_npe::hash_container(0, vec1);
    size_t hash2 = tt_npe::hash_container(0, vec2);
    size_t hash3 = tt_npe::hash_container(0, vec3);
    size_t hash4 = tt_npe::hash_container(0, vec4);
    
    // Same input should produce same hash
    EXPECT_EQ(hash1, hash2) << "hash_container failed to produce consistent results for identical inputs";
    
    // Different inputs should produce different hashes
    EXPECT_NE(hash1, hash3) << "hash_container failed to differentiate between different input orders";
    EXPECT_NE(hash1, hash4) << "hash_container failed to differentiate between slightly different inputs";
    
    // Test with different seed values
    size_t hash1_alt_seed = tt_npe::hash_container(42, vec1);
    EXPECT_NE(hash1, hash1_alt_seed) << "hash_container failed to incorporate seed value";
    
    // Test with vectors of custom types (Coord)
    std::vector<tt_npe::Coord> coords1 = {
        {0, 0, 0},
        {0, 0, 1},
        {0, 1, 0}
    };
    std::vector<tt_npe::Coord> coords2 = {
        {0, 0, 0},
        {0, 0, 1},
        {0, 1, 0}
    }; // Same as coords1
    std::vector<tt_npe::Coord> coords3 = {
        {0, 1, 0},
        {0, 0, 1},
        {0, 0, 0}
    }; // Different order
    
    // Compute hashes
    size_t coord_hash1 = tt_npe::hash_container(0, coords1);
    size_t coord_hash2 = tt_npe::hash_container(0, coords2);
    size_t coord_hash3 = tt_npe::hash_container(0, coords3);
    
    // Same input should produce same hash
    EXPECT_EQ(coord_hash1, coord_hash2) << "hash_container failed to produce consistent results for identical Coord inputs";
    
    // Different inputs should produce different hashes
    EXPECT_NE(coord_hash1, coord_hash3) << "hash_container failed to differentiate between different Coord input orders";
    
    // Test with empty containers
    std::vector<int> empty_vec;
    size_t empty_hash = tt_npe::hash_container(0, empty_vec);
    EXPECT_EQ(empty_hash, 0) << "hash_container should return seed for empty container";
}

TEST(npeUtilTest, HashFunctionQualityIntegers) {
    // Test the quality of hash_combine and hash_container functions
    // by checking distribution of hash values for randomly initialized integers
    
    const size_t NUM_SAMPLES = 10000;
    const size_t NUM_BUCKETS = 107;
    const double EXPECTED_BUCKET_SIZE = static_cast<double>(NUM_SAMPLES) / NUM_BUCKETS;
    const double MAX_DEVIATION = 0.3; // Allow up to 30% deviation from expected bucket size
    
    // Create a vector of random integers
    std::vector<size_t> random_ints;
    random_ints.reserve(NUM_SAMPLES);
    
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> int_dist(0, 12000000000000000000ull);
    
    // Generate random integers
    for (size_t i = 0; i < NUM_SAMPLES; i++) {
        size_t random_int = int_dist(gen);
        random_ints.emplace_back(random_int);
    }
    
    // Hash each integer and count occurrences in buckets
    std::vector<size_t> bucket_counts(NUM_BUCKETS, 0);
    
    // Test hash_mix function with integer
    for (const size_t& random_int : random_ints) {
        // Use the hash_combine function for integer
        size_t seed = 0;
        size_t hash_value = tt_npe::hash_combine(seed, random_int);
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
    // std::cout << "Hash mix distribution statistics for Coord components:" << std::endl;
    // std::cout << "  Min bucket count: " << min_count << std::endl;
    // std::cout << "  Max bucket count: " << max_count << std::endl;
    // std::cout << "  Expected bucket size: " << EXPECTED_BUCKET_SIZE << std::endl;
    // std::cout << "  Standard deviation: " << std_dev << std::endl;
    // std::cout << "  Normalized std dev: " << normalized_std_dev << std::endl;

    // Verify the distribution is reasonably uniform
    EXPECT_LT(normalized_std_dev, MAX_DEVIATION) 
        << "hash_mix function has poor distribution quality for Coord components";
}

TEST(npeUtilTest, HashFunctionQualityLinkAndNIUAttrs) {
    // Test the quality of hash functions for nocLinkAttr and nocNIUAttr
    // by checking distribution of hash values and collision rates
    
    const size_t NUM_SAMPLES = 10000;
    const size_t NUM_BUCKETS = 107;
    const double EXPECTED_BUCKET_SIZE = static_cast<double>(NUM_SAMPLES) / NUM_BUCKETS;
    const double MAX_DEVIATION = 0.3; // Allow up to 30% deviation from expected bucket size
    
    // Define bounds for device_id, row, col, and types
    const int MAX_DEVICE_ID = 10;
    const int16_t MAX_ROW = 20;
    const int16_t MAX_COL = 20;
    
    // Get all possible link types and NIU types
    std::vector<nocLinkType> link_types = {
        nocLinkType::NOC1_NORTH,
        nocLinkType::NOC1_WEST,
        nocLinkType::NOC0_EAST,
        nocLinkType::NOC0_SOUTH,
    };
    
    std::vector<nocNIUType> niu_types = {
        nocNIUType::NOC0_SRC,
        nocNIUType::NOC0_SINK,
        nocNIUType::NOC1_SRC,
        nocNIUType::NOC1_SINK,
    };
    
    // Create vectors of random nocLinkAttr and nocNIUAttr objects
    boost::unordered_flat_set<nocLinkAttr> link_attrs;
    boost::unordered_flat_set<nocNIUAttr> niu_attrs;
    
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> device_id_dist(0, MAX_DEVICE_ID);
    std::uniform_int_distribution<int16_t> row_dist(0, MAX_ROW);
    std::uniform_int_distribution<int16_t> col_dist(0, MAX_COL);
    std::uniform_int_distribution<size_t> link_type_dist(0, link_types.size() - 1);
    std::uniform_int_distribution<size_t> niu_type_dist(0, niu_types.size() - 1);
    
    // Generate distinct random nocLinkAttr objects
    while (link_attrs.size() < NUM_SAMPLES) {
        DeviceID device_id = DeviceID(device_id_dist(gen));
        int16_t row = row_dist(gen);
        int16_t col = col_dist(gen);
        nocLinkType type = link_types[link_type_dist(gen)];
        
        nocLinkAttr attr{{device_id, row, col}, type};
        link_attrs.insert(attr);
    }
    
    // Generate distinct random nocNIUAttr objects
    while (niu_attrs.size() < NUM_SAMPLES) {
        DeviceID device_id = DeviceID(device_id_dist(gen));
        int16_t row = row_dist(gen);
        int16_t col = col_dist(gen);
        nocNIUType type = niu_types[niu_type_dist(gen)];
        
        nocNIUAttr attr{{device_id, row, col}, type};
        niu_attrs.insert(attr);
    }
    
    // Test hash distribution for nocLinkAttr
    {
        std::vector<size_t> bucket_counts(NUM_BUCKETS, 0);
        
        for (const auto& attr : link_attrs) {
            size_t hash_value = std::hash<nocLinkAttr>{}(attr);
            size_t bucket = hash_value % NUM_BUCKETS;
            bucket_counts[bucket]++;
        }
        
        // Check distribution quality
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
        // std::cout << "\nHash distribution statistics for nocLinkAttr:" << std::endl;
        // std::cout << "  Min bucket count: " << min_count << std::endl;
        // std::cout << "  Max bucket count: " << max_count << std::endl;
        // std::cout << "  Expected bucket size: " << EXPECTED_BUCKET_SIZE << std::endl;
        // std::cout << "  Standard deviation: " << std_dev << std::endl;
        // std::cout << "  Normalized std dev: " << normalized_std_dev << std::endl;
        
        // Verify the distribution is reasonably uniform
        EXPECT_LT(normalized_std_dev, MAX_DEVIATION) 
            << "std::hash<nocLinkAttr> has poor distribution quality";
    }
    
    // Test hash distribution for nocNIUAttr
    {
        std::vector<size_t> bucket_counts(NUM_BUCKETS, 0);
        
        for (const auto& attr : niu_attrs) {
            size_t hash_value = std::hash<nocNIUAttr>{}(attr);
            size_t bucket = hash_value % NUM_BUCKETS;
            bucket_counts[bucket]++;
        }
        
        // Check distribution quality
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
        // std::cout << "\nHash distribution statistics for nocNIUAttr:" << std::endl;
        // std::cout << "  Min bucket count: " << min_count << std::endl;
        // std::cout << "  Max bucket count: " << max_count << std::endl;
        // std::cout << "  Expected bucket size: " << EXPECTED_BUCKET_SIZE << std::endl;
        // std::cout << "  Standard deviation: " << std_dev << std::endl;
        // std::cout << "  Normalized std dev: " << normalized_std_dev << std::endl;
        
        // Verify the distribution is reasonably uniform
        EXPECT_LT(normalized_std_dev, MAX_DEVIATION) 
            << "std::hash<nocNIUAttr> has poor distribution quality";
    }
    
    // Test for collisions in nocLinkAttr objects
    {
        boost::unordered_flat_set<size_t> unique_hashes;
        for (const auto& attr : link_attrs) {
            size_t hash_value = std::hash<nocLinkAttr>{}(attr);
            unique_hashes.insert(hash_value);
        }
        
        // Calculate collision rate
        double collision_rate = 1.0 - (static_cast<double>(unique_hashes.size()) / link_attrs.size());
        // std::cout << "\nCollision rate for nocLinkAttr objects: " << (collision_rate * 100.0) << "%" << std::endl;
        
        // Expect collision rate to be very low (less than 1%)
        EXPECT_LT(collision_rate, 0.01) << "std::hash<nocLinkAttr> has high collision rate";
    }
    
    // Test for collisions in nocNIUAttr objects
    {
        boost::unordered_flat_set<size_t> unique_hashes;
        for (const auto& attr : niu_attrs) {
            size_t hash_value = std::hash<nocNIUAttr>{}(attr);
            unique_hashes.insert(hash_value);
        }
        
        // Calculate collision rate
        double collision_rate = 1.0 - (static_cast<double>(unique_hashes.size()) / niu_attrs.size());
        // std::cout << "\nCollision rate for nocNIUAttr objects: " << (collision_rate * 100.0) << "%" << std::endl;
        
        // Expect collision rate to be very low (less than 1%)
        EXPECT_LT(collision_rate, 0.01) << "std::hash<nocNIUAttr> has high collision rate";
    }
}

}  // namespace tt_npe

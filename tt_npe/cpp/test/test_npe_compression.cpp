// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "gtest/gtest.h"
#include "npeCompressionUtil.hpp"
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <filesystem>
#include <cstdio>
#include "zstd.h"

namespace tt_npe {

// Helper function to read file contents
std::string readFileContents(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return "";
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read file content
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    
    return std::string(buffer.data(), size);
}

// Helper function to decompress zstd compressed data
std::string decompressZstd(const std::string& compressedData) {
    // Get decompressed size
    unsigned long long decompressedSize = ZSTD_getFrameContentSize(compressedData.data(), compressedData.size());
    if (decompressedSize == ZSTD_CONTENTSIZE_ERROR || decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        return "";
    }
    
    // Allocate buffer for decompressed data
    std::vector<char> decompressedBuffer(decompressedSize);
    
    // Decompress
    size_t result = ZSTD_decompress(
        decompressedBuffer.data(), decompressedSize,
        compressedData.data(), compressedData.size()
    );
    
    if (ZSTD_isError(result)) {
        return "";
    }
    
    return std::string(decompressedBuffer.data(), result);
}

class NpeCompressionUtilTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        tempDir = std::filesystem::temp_directory_path() / "npe_compression_test";
        std::filesystem::create_directories(tempDir);
    }
    
    void TearDown() override {
        // Clean up temporary files
        std::filesystem::remove_all(tempDir);
    }
    
    std::filesystem::path tempDir;
};

TEST_F(NpeCompressionUtilTest, CompressSmallString) {
    std::string content = "Hello, world!";
    std::string filepath = (tempDir / "small.zst").string();
    
    // Compress
    bool success = npeCompressionUtil::compressToFile(content, filepath);
    EXPECT_TRUE(success);
    
    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(filepath));
    
    // Read compressed data
    std::string compressedData = readFileContents(filepath);
    EXPECT_FALSE(compressedData.empty());
    
    // Decompress and verify
    std::string decompressedData = decompressZstd(compressedData);
    EXPECT_EQ(decompressedData, content);
}

TEST_F(NpeCompressionUtilTest, CompressLargeString) {
    // Generate a large random string
    const size_t dataSize = 1024 * 1024; // 1MB
    std::string content(dataSize, 'A');
    
    // Add some randomness to make it more compressible
    std::mt19937 rng(42); // Fixed seed for reproducibility
    std::uniform_int_distribution<int> dist(0, 255);
    
    for (size_t i = 0; i < dataSize; i += 100) {
        content[i] = static_cast<char>(dist(rng));
    }
    
    std::string filepath = (tempDir / "large.zst").string();
    
    // Compress
    bool success = npeCompressionUtil::compressToFile(content, filepath);
    EXPECT_TRUE(success);
    
    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(filepath));
    
    // Read compressed data
    std::string compressedData = readFileContents(filepath);
    EXPECT_FALSE(compressedData.empty());
    
    // Verify compressed size is smaller than original
    EXPECT_LT(compressedData.size(), content.size());
    
    // Decompress and verify
    std::string decompressedData = decompressZstd(compressedData);
    EXPECT_EQ(decompressedData, content);
}

TEST_F(NpeCompressionUtilTest, CompressToInvalidPath) {
    std::string content = "Test content";
    std::string filepath = "/nonexistent/directory/file.zst";
    
    // Compression should fail due to invalid path
    bool success = npeCompressionUtil::compressToFile(content, filepath);
    EXPECT_FALSE(success);
}

TEST_F(NpeCompressionUtilTest, CompressWithSpecialCharacters) {
    // Create a string with special characters
    std::string content = "Special characters: !@#$%^&*()_+{}|:\"<>?[];',./\n\t\r";
    std::string filepath = (tempDir / "special.zst").string();
    
    // Compress
    bool success = npeCompressionUtil::compressToFile(content, filepath);
    EXPECT_TRUE(success);
    
    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(filepath));
    
    // Read compressed data
    std::string compressedData = readFileContents(filepath);
    EXPECT_FALSE(compressedData.empty());
    
    // Decompress and verify
    std::string decompressedData = decompressZstd(compressedData);
    EXPECT_EQ(decompressedData, content);
}

TEST_F(NpeCompressionUtilTest, CompressRepeatedContent) {
    // Create a highly compressible string with repeated content
    std::string repeatedPattern = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string content;
    for (int i = 0; i < 1000; i++) {
        content += repeatedPattern;
    }
    
    std::string filepath = (tempDir / "repeated.zst").string();
    
    // Compress
    bool success = npeCompressionUtil::compressToFile(content, filepath);
    EXPECT_TRUE(success);
    
    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(filepath));
    
    // Read compressed data
    std::string compressedData = readFileContents(filepath);
    EXPECT_FALSE(compressedData.empty());
    
    // Verify high compression ratio for repeated content
    double compressionRatio = static_cast<double>(content.size()) / compressedData.size();
    EXPECT_GT(compressionRatio, 10.0); // Should achieve at least 10:1 compression
    
    // Decompress and verify
    std::string decompressedData = decompressZstd(compressedData);
    EXPECT_EQ(decompressedData, content);
}

TEST_F(NpeCompressionUtilTest, CompressOverwriteExistingFile) {
    std::string content1 = "First content";
    std::string content2 = "Second content - completely different";
    std::string filepath = (tempDir / "overwrite.zst").string();
    
    // First compression
    bool success1 = npeCompressionUtil::compressToFile(content1, filepath);
    EXPECT_TRUE(success1);
    
    // Read first compressed data
    std::string compressedData1 = readFileContents(filepath);
    
    // Second compression (overwrite)
    bool success2 = npeCompressionUtil::compressToFile(content2, filepath);
    EXPECT_TRUE(success2);
    
    // Read second compressed data
    std::string compressedData2 = readFileContents(filepath);
    
    // Verify compressed data changed
    EXPECT_NE(compressedData1, compressedData2);
    
    // Decompress and verify
    std::string decompressedData = decompressZstd(compressedData2);
    EXPECT_EQ(decompressedData, content2);
}

} // namespace tt_npe

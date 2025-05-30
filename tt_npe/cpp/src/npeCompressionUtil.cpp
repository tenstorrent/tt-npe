// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeCompressionUtil.hpp"
#include <fstream>
#include <vector>
#include "zstd.h"
#include <stdexcept> // For error reporting, though we return bool
#include <iostream> // For potential error logging
#include "npeAssert.hpp"

namespace tt_npe {
bool npeCompressionUtil::compressToFile(const std::string& contents, const std::string& filepath) {
    size_t const inputSize = contents.size();
    TT_ASSERT(inputSize > 0, "Input content is empty");

    // Determine the maximum potential compressed size
    size_t const maxCompressedSize = ZSTD_compressBound(inputSize);
    if (ZSTD_isError(maxCompressedSize)) {
        std::cerr << "Error determining compression bound: " << ZSTD_getErrorName(maxCompressedSize) << std::endl;
        return false;
    }

    // Allocate buffer for compressed data
    std::vector<char> compressedBuffer(maxCompressedSize);

    // Perform compression
    size_t const compressedSize = ZSTD_compress(
        compressedBuffer.data(),
        maxCompressedSize,
        contents.data(),
        inputSize,
        ZSTD_CLEVEL_DEFAULT // Default compression level
    );

    // Check for compression errors
    if (ZSTD_isError(compressedSize)) {
        std::cerr << "Compression error: " << ZSTD_getErrorName(compressedSize) << std::endl;
        return false;
    }

    // Write compressed data to file
    std::ofstream outFile(filepath, std::ios::binary | std::ios::trunc);
    if (!outFile) {
        std::cerr << "Error opening file for writing: " << filepath << std::endl;
        return false;
    }

    outFile.write(compressedBuffer.data(), compressedSize);
    if (!outFile.good()) {
        std::cerr << "Error writing compressed data to file: " << filepath << std::endl;
        outFile.close(); // Attempt to close before returning
        return false;
    }

    outFile.close();
    return true;
}
} // namespace tt_npe

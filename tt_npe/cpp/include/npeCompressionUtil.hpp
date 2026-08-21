// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace tt_npe {

class npeStreamingFileWriter {
public:
    npeStreamingFileWriter(const std::string& filepath, bool compress);
    ~npeStreamingFileWriter();

    npeStreamingFileWriter(const npeStreamingFileWriter&) = delete;
    npeStreamingFileWriter& operator=(const npeStreamingFileWriter&) = delete;

    bool good() const;
    bool write(std::string_view chunk);
    bool close();

private:
    friend class NpeCompressionUtilTest_CoalescesManyTinyWritesBeforeCompressing_Test;

    size_t compressionCallCount() const;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Utility class for compression operations using zstd.
 */
class npeCompressionUtil {
public:
    /**
     * @brief Compresses the given string contents and writes them to a file using zstd.
     *
     * @param contents The string data to compress.
     * @param filepath The path to the file where compressed data will be written.
     * @return True if compression and file writing were successful, false otherwise.
     */
    static bool compressToFile(const std::string& contents, const std::string& filepath);

private:
    // Prevent instantiation
    npeCompressionUtil() = delete;
    ~npeCompressionUtil() = delete;
    npeCompressionUtil(const npeCompressionUtil&) = delete;
    npeCompressionUtil& operator=(const npeCompressionUtil&) = delete;
};

} // namespace tt_npe

#pragma once

#include <string>

namespace tt_npe {

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

// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeCompressionUtil.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "npeAssert.hpp"
#include "zstd.h"

namespace tt_npe {

class npeStreamingFileWriter::Impl {
public:
    Impl(const std::string& filepath, bool compress) :
        output_(filepath, std::ios::binary | std::ios::trunc),
        compress_(compress) {
        if (compress_ && output_) {
            compression_context_ = ZSTD_createCCtx();
            input_buffer_.resize(ZSTD_CStreamInSize());
            output_buffer_.resize(ZSTD_CStreamOutSize());
        }
    }

    ~Impl() {
        close();
        ZSTD_freeCCtx(compression_context_);
    }

    bool good() const {
        return output_.good() && (!compress_ || compression_context_ != nullptr) && !failed_;
    }

    bool write(std::string_view chunk) {
        if (closed_ || !good()) {
            return false;
        }
        if (!compress_) {
            output_.write(chunk.data(), chunk.size());
            failed_ = !output_.good();
            return !failed_;
        }

        size_t chunk_offset = 0;
        while (chunk_offset < chunk.size()) {
            if (input_buffer_size_ != 0) {
                size_t copy_size = std::min(
                    input_buffer_.size() - input_buffer_size_,
                    chunk.size() - chunk_offset);
                std::memcpy(
                    input_buffer_.data() + input_buffer_size_,
                    chunk.data() + chunk_offset,
                    copy_size);
                input_buffer_size_ += copy_size;
                chunk_offset += copy_size;
                if (input_buffer_size_ == input_buffer_.size()) {
                    if (!compressInput(input_buffer_.data(), input_buffer_size_)) {
                        return false;
                    }
                    input_buffer_size_ = 0;
                }
                continue;
            }

            size_t remaining = chunk.size() - chunk_offset;
            if (remaining >= input_buffer_.size()) {
                if (!compressInput(
                        chunk.data() + chunk_offset, input_buffer_.size())) {
                    return false;
                }
                chunk_offset += input_buffer_.size();
                continue;
            }

            std::memcpy(input_buffer_.data(), chunk.data() + chunk_offset, remaining);
            input_buffer_size_ = remaining;
            chunk_offset += remaining;
        }
        return true;
    }

    bool close() {
        if (closed_) {
            return !failed_;
        }
        if (compress_ && compression_context_ != nullptr && !failed_ &&
            input_buffer_size_ != 0) {
            if (!compressInput(input_buffer_.data(), input_buffer_size_)) {
                failed_ = true;
            }
            input_buffer_size_ = 0;
        }
        if (compress_ && compression_context_ != nullptr && !failed_) {
            ZSTD_inBuffer input{nullptr, 0, 0};
            size_t remaining = 1;
            while (remaining != 0) {
                ZSTD_outBuffer output{output_buffer_.data(), output_buffer_.size(), 0};
                ++compression_call_count_;
                remaining =
                    ZSTD_compressStream2(compression_context_, &output, &input, ZSTD_e_end);
                if (ZSTD_isError(remaining)) {
                    failed_ = true;
                    break;
                }
                output_.write(output_buffer_.data(), output.pos);
                if (!output_.good()) {
                    failed_ = true;
                    break;
                }
            }
        }
        output_.close();
        failed_ = failed_ || output_.fail();
        closed_ = true;
        return !failed_;
    }

    size_t compressionCallCount() const {
        return compression_call_count_;
    }

private:
    bool compressInput(const char* data, size_t size) {
        ZSTD_inBuffer input{data, size, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output{output_buffer_.data(), output_buffer_.size(), 0};
            ++compression_call_count_;
            size_t result =
                ZSTD_compressStream2(compression_context_, &output, &input, ZSTD_e_continue);
            if (ZSTD_isError(result)) {
                failed_ = true;
                return false;
            }
            output_.write(output_buffer_.data(), output.pos);
            if (!output_.good()) {
                failed_ = true;
                return false;
            }
        }
        return true;
    }

    std::ofstream output_;
    bool compress_;
    ZSTD_CCtx* compression_context_ = nullptr;
    std::vector<char> input_buffer_;
    std::vector<char> output_buffer_;
    size_t input_buffer_size_ = 0;
    size_t compression_call_count_ = 0;
    bool failed_ = false;
    bool closed_ = false;
};

npeStreamingFileWriter::npeStreamingFileWriter(
    const std::string& filepath, bool compress) :
    impl_(std::make_unique<Impl>(filepath, compress)) {}

npeStreamingFileWriter::~npeStreamingFileWriter() = default;

bool npeStreamingFileWriter::good() const {
    return impl_->good();
}

bool npeStreamingFileWriter::write(std::string_view chunk) {
    return impl_->write(chunk);
}

bool npeStreamingFileWriter::close() {
    return impl_->close();
}

size_t npeStreamingFileWriter::compressionCallCount() const {
    return impl_->compressionCallCount();
}

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

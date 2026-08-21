// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <concepts>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "nlohmann/json.hpp"
#include "npeCompressionUtil.hpp"

namespace tt_npe {

class TimelineJsonWriter {
public:
    TimelineJsonWriter(const std::string& filepath, bool compress) :
        output_(filepath, compress) {
        valid_ = output_.good() && output_.write("{");
        contexts_.push_back({ContextType::Object, true});
    }

    template <typename Json>
    bool writeField(std::string_view name, const Json& value) {
        return flushIntegralBuffer() && writeFieldPrefix(name) &&
               output_.write(dumpValue(value));
    }

    bool beginArrayField(std::string_view name) {
        if (!flushIntegralBuffer() || !writeFieldPrefix(name) ||
            !output_.write("[")) {
            return false;
        }
        contexts_.push_back({ContextType::Array, true});
        return true;
    }

    template <typename Json>
        requires(
            !std::integral<std::remove_cvref_t<Json>> ||
            std::same_as<std::remove_cvref_t<Json>, bool>)
    bool writeArrayItem(const Json& value) {
        return flushIntegralBuffer() && writeArrayItemPrefix() &&
               output_.write(dumpValue(value));
    }

    template <std::integral Integer>
        requires(!std::same_as<std::remove_cv_t<Integer>, bool>)
    bool writeArrayItem(Integer value) {
        if (!valid_ || contexts_.empty() ||
            contexts_.back().type != ContextType::Array) {
            return false;
        }

        std::array<char, 32> buffer;
        char* value_begin = buffer.data();
        if (!contexts_.back().first_item) {
            *value_begin++ = ',';
        }
        auto result = std::to_chars(value_begin, buffer.data() + buffer.size(), value);
        if (result.ec != std::errc{}) {
            return false;
        }
        size_t item_size = result.ptr - buffer.data();
        if (integral_buffer_size_ + item_size > integral_buffer_.size() &&
            !flushIntegralBuffer()) {
            return false;
        }
        std::memcpy(
            integral_buffer_.data() + integral_buffer_size_,
            buffer.data(),
            item_size);
        integral_buffer_size_ += item_size;
        contexts_.back().first_item = false;
        return true;
    }

    bool beginObjectItem() {
        if (!flushIntegralBuffer() || !writeArrayItemPrefix() ||
            !output_.write("{")) {
            return false;
        }
        contexts_.push_back({ContextType::Object, true});
        return true;
    }

    bool endObject() {
        if (contexts_.size() <= 1 ||
            contexts_.back().type != ContextType::Object) {
            return false;
        }
        if (!flushIntegralBuffer()) {
            return false;
        }
        contexts_.pop_back();
        return output_.write("}");
    }

    bool endArray() {
        if (contexts_.empty() || contexts_.back().type != ContextType::Array) {
            return false;
        }
        if (!flushIntegralBuffer()) {
            return false;
        }
        contexts_.pop_back();
        return output_.write("]");
    }

    bool close() {
        return valid_ && contexts_.size() == 1 &&
               contexts_.back().type == ContextType::Object &&
               flushIntegralBuffer() && output_.write("}") && output_.close();
    }

private:
    enum class ContextType { Object, Array };

    struct Context {
        ContextType type;
        bool first_item;
    };

    template <typename Value>
    static std::string dumpValue(const Value& value) {
        if constexpr (requires { value.dump(); }) {
            return value.dump();
        } else {
            return nlohmann::json(value).dump();
        }
    }

    bool writeFieldPrefix(std::string_view name) {
        if (!valid_ || contexts_.empty() ||
            contexts_.back().type != ContextType::Object) {
            return false;
        }
        if (!contexts_.back().first_item && !output_.write(",")) {
            return false;
        }
        contexts_.back().first_item = false;
        return output_.write(nlohmann::json(name).dump()) && output_.write(":");
    }

    bool writeArrayItemPrefix() {
        if (!valid_ || contexts_.empty() ||
            contexts_.back().type != ContextType::Array) {
            return false;
        }
        if (!contexts_.back().first_item && !output_.write(",")) {
            return false;
        }
        contexts_.back().first_item = false;
        return true;
    }

    bool flushIntegralBuffer() {
        if (integral_buffer_size_ == 0) {
            return true;
        }
        if (!output_.write(
                std::string_view(
                    integral_buffer_.data(), integral_buffer_size_))) {
            return false;
        }
        integral_buffer_size_ = 0;
        return true;
    }

    npeStreamingFileWriter output_;
    std::vector<Context> contexts_;
    std::array<char, 4096> integral_buffer_;
    size_t integral_buffer_size_ = 0;
    bool valid_ = false;
};

}  // namespace tt_npe

#pragma once

#include "grid.hpp"
#include "nocNode.hpp"

namespace tt_npe {

class nocModel {
public:
  nocModel(size_t gridx, size_t gridy) : noc_grid(gridx, gridy) {}

private:
  Grid2D<nocNode> noc_grid;
};

// Main logging function template
template <typename... Args>
void log(LogLevel level, const std::filesystem::path &file, int line,
         fmt::format_string<Args...> format, Args &&...args) {
  auto now = std::chrono::system_clock::now();
  auto filename = file.filename().string();

  // Format: [TIME] [LEVEL] [FILE:LINE] Message
  fmt::print("[{:%Y-%m-%d %H:%M:%S}] [{}] [{}:{}] {}\n", now,
             logLevelToString(level), filename, line,
             fmt::format(format, std::forward<Args>(args)...));
}

// Convenience macros to automatically capture file and line
#define LOG_DEBUG(...) log(LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) log(LogLevel::INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) log(LogLevel::WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log(LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
} // namespace tt_npe
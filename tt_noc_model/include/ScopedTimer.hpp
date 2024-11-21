#pragma once

#include <chrono>
#include <fmt/core.h>

class ScopedTimer {
private:
  std::chrono::steady_clock::time_point start_time;
  std::string name;
  bool printed;

public:
  explicit ScopedTimer(std::string &&timer_name = "")
      : start_time(std::chrono::steady_clock::now()),
        name(std::move(timer_name)), printed(false) {}

  ~ScopedTimer() {
    if (not printed) {
      printDelta();
    }
  }

  void printDelta() {
    printed = true;
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time);
    fmt::println("Timer {} : {} μs", name, duration.count());
  }

  ScopedTimer(const ScopedTimer &) = delete;
  ScopedTimer &operator=(const ScopedTimer &) = delete;
  ScopedTimer(ScopedTimer &&) = default;
  ScopedTimer &operator=(ScopedTimer &&) = default;
};

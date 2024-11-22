#pragma once

#include <fmt/core.h>

#include <chrono>

class ScopedTimer {
   private:
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::string name;
    bool stopped;

   public:
    explicit ScopedTimer(std::string &&timer_name = "") :
        start_time(std::chrono::steady_clock::now()), name(std::move(timer_name)), stopped(false) {}

    ~ScopedTimer() {
        if (not stopped) {
            stop();
            printDelta();
        }
    }

    // stops the timer
    void stop() {
        if (!stopped) {
            stopped = true;
            end_time = std::chrono::steady_clock::now();
        }
    }
    // stops the timer and returns elapsed time in microseconds
    size_t getElapsedTimeMicroSeconds() {
        return std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    }
    // stops the timer and prints status
    void printDelta() {
        stop();
        fmt::println("Timer {} : {} μs", name, getElapsedTimeMicroSeconds());
    }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;
    ScopedTimer(ScopedTimer &&) = default;
    ScopedTimer &operator=(ScopedTimer &&) = default;
};

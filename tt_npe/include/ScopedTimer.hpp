#pragma once

#include <fmt/core.h>

#include <chrono>

class ScopedTimer {

   public:
    explicit ScopedTimer(std::string &&timer_name = "", bool silence_output = false) :
        start_time(std::chrono::steady_clock::now()), name(std::move(timer_name)), stopped(false), silent(silence_output) {}

    ~ScopedTimer() {
        if (not stopped) {
            stop();
            if (silent) { printDelta(); }
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

   private:
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::string name;
    bool stopped;
    bool silent;
};

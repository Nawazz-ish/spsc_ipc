#pragma once

#include <cstdint>
#include <algorithm>
#include <vector>
#include <time.h>
#include <thread>
#include <chrono>

// Read the CPU's time-stamp counter using rdtscp.
// rdtscp serializes — waits for all prior instructions to retire
// before reading the counter, so the timestamp is meaningful.
static inline uint64_t rdtscp_now() {
    uint32_t lo, hi, aux;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (uint64_t(hi) << 32) | lo;
}

struct TscCalibration {
    double ticks_per_ns;
};

// Calibrate the TSC frequency against CLOCK_MONOTONIC.
// Takes `samples` measurements of `sleep_ms` each, returns the median.
inline TscCalibration calibrate_tsc(int samples = 5, int sleep_ms = 100) {
    std::vector<double> rates;
    rates.reserve(samples);

    for (int i = 0; i < samples; ++i) {
        timespec ts_start, ts_end;
        uint64_t tsc_start, tsc_end;

        clock_gettime(CLOCK_MONOTONIC, &ts_start);
        tsc_start = rdtscp_now();

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

        tsc_end = rdtscp_now();
        clock_gettime(CLOCK_MONOTONIC, &ts_end);

        int64_t ns_elapsed = (ts_end.tv_sec - ts_start.tv_sec) * 1'000'000'000LL
                           + (ts_end.tv_nsec - ts_start.tv_nsec);
        uint64_t ticks_elapsed = tsc_end - tsc_start;

        rates.push_back(double(ticks_elapsed) / double(ns_elapsed));
    }

    std::sort(rates.begin(), rates.end());
    return TscCalibration{ rates[rates.size() / 2] };
}

// Convert a tick delta to nanoseconds.
static inline int64_t ticks_to_ns(uint64_t ticks, double ticks_per_ns) {
    return int64_t(double(ticks) / ticks_per_ns);
}

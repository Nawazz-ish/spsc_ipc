#include "../shared/include/shm.hpp"
#include "../shared/include/shm_layout.hpp"
#include "../shared/include/tsc.hpp"
#include "../shared/include/histogram.hpp"

#include <pthread.h>
#include <sched.h>
#include <immintrin.h>
#include <cstdio>
#include <cstdint>

static constexpr size_t   WARMUP_SAMPLES  = 10'000;
static constexpr size_t   MEASURE_SAMPLES = 1'000'000;
static constexpr uint64_t STOP_SENTINEL   = UINT64_MAX;
static constexpr int      CONSUMER_CORE   = 1;

static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

int main() {
    printf("[consumer] attaching to %s...\n", SHM_NAME);
    void* mem = shm_attach(SHM_NAME, sizeof(SHMLayout), true);
    auto* layout = static_cast<SHMLayout*>(mem);

    printf("[consumer] init_or_wait...\n");
    double ticks_per_ns = init_or_wait(layout);
    printf("[consumer] ticks_per_ns = %.6f\n", ticks_per_ns);

    pin_to_core(CONSUMER_CORE);

    Histogram hist;
    hist.reserve(MEASURE_SAMPLES);
    Message m;

    for (size_t i = 0; i < WARMUP_SAMPLES; ++i) {
        while (!layout->cpp_to_rust.try_pop(m)) _mm_pause();
    }

    // Record raw tick-deltas; convert to ns at report time, off the hot path.
    for (size_t i = 0; i < MEASURE_SAMPLES; ++i) {
        while (!layout->cpp_to_rust.try_pop(m)) _mm_pause();
        uint64_t now = rdtscp_now();
        hist.record(int64_t(now - m.tsc));
    }

    while (true) {
        if (layout->cpp_to_rust.try_pop(m) && m.seq == STOP_SENTINEL) break;
        _mm_pause();
    }

    hist.report("shm one-way latency", ticks_per_ns);
    return 0;
}

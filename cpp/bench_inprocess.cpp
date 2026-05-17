#include "../shared/include/spsc_queue.hpp"
#include "../shared/include/tsc.hpp"
#include "../shared/include/histogram.hpp"
#include "../shared/include/message.hpp"

#include <pthread.h>
#include <sched.h>
#include <immintrin.h>
#include <memory>
#include <thread>
#include <cstdio>
#include <cstdint>

static constexpr size_t QUEUE_CAPACITY  = 4096;
static constexpr size_t WARMUP_SAMPLES  = 10'000;
static constexpr size_t MEASURE_SAMPLES = 1'000'000;
static constexpr uint64_t STOP_SENTINEL = UINT64_MAX;

using Queue = SPSCQueue<Message, QUEUE_CAPACITY>;

static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        fprintf(stderr, "warning: failed to pin to core %d\n", core);
    }
}

int main() {
    // ── 1. Calibrate the TSC ──
    printf("Calibrating TSC...\n");
    auto cal = calibrate_tsc();
    printf("  ticks_per_ns = %.6f  (~%.3f GHz)\n\n", cal.ticks_per_ns, cal.ticks_per_ns);

    // ── 2. Allocate the queue on the heap ──
    auto queue = std::make_unique<Queue>();
    Histogram hist;
    hist.reserve(MEASURE_SAMPLES);

    // ── 3. Consumer thread ──
    std::thread consumer([&] {
        pin_to_core(1);
        Message m;

        // Discard warmup samples.
        for (size_t i = 0; i < WARMUP_SAMPLES; ++i) {
            while (!queue->try_pop(m)) _mm_pause();
        }

        // Measure: record cycle-delta from producer's timestamp to now.
        for (size_t i = 0; i < MEASURE_SAMPLES; ++i) {
            while (!queue->try_pop(m)) _mm_pause();
            uint64_t now = rdtscp_now();
            int64_t latency_ns = ticks_to_ns(now - m.tsc, cal.ticks_per_ns);
            hist.record(latency_ns);
        }

        // Wait for the stop sentinel and exit.
        while (true) {
            if (queue->try_pop(m) && m.seq == STOP_SENTINEL) break;
            _mm_pause();
        }
    });

    // ── 4. Producer (main thread) ──
    pin_to_core(0);
    Message m{};

    // Warmup pushes.
    for (size_t i = 0; i < WARMUP_SAMPLES; ++i) {
        m.tsc = rdtscp_now();
        m.seq = i;
        while (!queue->try_push(m)) _mm_pause();
    }

    // Measured pushes.
    for (size_t i = 0; i < MEASURE_SAMPLES; ++i) {
        m.tsc = rdtscp_now();
        m.seq = i;
        while (!queue->try_push(m)) _mm_pause();
    }

    // Send the stop sentinel.
    m.seq = STOP_SENTINEL;
    m.tsc = 0;
    while (!queue->try_push(m)) _mm_pause();

    consumer.join();

    // ── 5. Report ──
    hist.report("in-process latency");
    return 0;
}

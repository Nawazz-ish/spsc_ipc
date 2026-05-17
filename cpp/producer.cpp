#include "../shared/include/shm.hpp"
#include "../shared/include/shm_layout.hpp"
#include "../shared/include/tsc.hpp"

#include <pthread.h>
#include <sched.h>
#include <immintrin.h>
#include <cstdio>
#include <cstdint>

static constexpr size_t   WARMUP_SAMPLES  = 10'000;
static constexpr size_t   MEASURE_SAMPLES = 1'000'000;
static constexpr uint64_t STOP_SENTINEL   = UINT64_MAX;
static constexpr int      PRODUCER_CORE   = 0;

static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

int main() {
    printf("[producer] attaching to %s...\n", SHM_NAME);
    void* mem = shm_attach(SHM_NAME, sizeof(SHMLayout), true);
    auto* layout = static_cast<SHMLayout*>(mem);

    printf("[producer] init_or_wait...\n");
    double ticks_per_ns = init_or_wait(layout);
    printf("[producer] ticks_per_ns = %.6f\n", ticks_per_ns);

    pin_to_core(PRODUCER_CORE);

    Message m{};

    for (size_t i = 0; i < WARMUP_SAMPLES; ++i) {
        m.tsc = rdtscp_now();
        m.seq = i;
        while (!layout->cpp_to_rust.try_push(m)) _mm_pause();
    }

    for (size_t i = 0; i < MEASURE_SAMPLES; ++i) {
        m.tsc = rdtscp_now();
        m.seq = i;
        while (!layout->cpp_to_rust.try_push(m)) _mm_pause();
    }

    m.seq = STOP_SENTINEL;
    m.tsc = 0;
    while (!layout->cpp_to_rust.try_push(m)) _mm_pause();

    printf("[producer] done. sent %zu messages.\n",
           WARMUP_SAMPLES + MEASURE_SAMPLES + 1);
    return 0;
}

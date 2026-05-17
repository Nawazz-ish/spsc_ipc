#include "../shared/include/shm.hpp"
#include "../shared/include/shm_layout.hpp"
#include "../shared/include/tsc.hpp"
#include "../shared/include/histogram.hpp"

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <immintrin.h>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static constexpr size_t   WARMUP_SAMPLES  = 10'000;
static constexpr size_t   MEASURE_SAMPLES = 1'000'000;
static constexpr uint64_t STOP_SENTINEL   = UINT64_MAX;
static constexpr int      INITIATOR_CORE  = 2;
static constexpr int      RESPONDER_CORE  = 3;

static void pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

// Tuning knobs, all opt-in via env vars so the same binary serves baseline +
// every Phase 6b variation.
static bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] == '1';
}

static void maybe_mlock(void* mem, size_t bytes, const char* role) {
    if (!env_flag("PP_MLOCK")) return;
    if (mlock(mem, bytes) != 0) {
        std::fprintf(stderr, "[cpp-%s] mlock failed: %s\n", role, std::strerror(errno));
    } else {
        std::fprintf(stderr, "[cpp-%s] mlock OK (%zu bytes)\n", role, bytes);
    }
}

static void maybe_sched_fifo(const char* role) {
    if (!env_flag("PP_FIFO")) return;
    sched_param sp{};
    sp.sched_priority = 50;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        std::fprintf(stderr, "[cpp-%s] SCHED_FIFO failed: %s "
                             "(need CAP_SYS_NICE: sudo setcap cap_sys_nice=ep <binary>)\n",
                     role, std::strerror(errno));
    } else {
        std::fprintf(stderr, "[cpp-%s] SCHED_FIFO prio=50 OK\n", role);
    }
}

static int run_initiator(SHMLayout* layout, double ticks_per_ns) {
    pin_to_core(INITIATOR_CORE);
    maybe_sched_fifo("initiator");

    Histogram hist;
    hist.reserve(MEASURE_SAMPLES);
    Message tx{}, rx{};

    // Warmup — same code path as measurement, samples discarded.
    for (size_t i = 0; i < WARMUP_SAMPLES; ++i) {
        tx.seq = i;
        while (!layout->cpp_to_rust.try_push(tx)) _mm_pause();
        while (!layout->rust_to_cpp.try_pop(rx))  _mm_pause();
    }

    // Measure: queue depth is mechanically ≤1, so no pacing needed.
    for (size_t i = 0; i < MEASURE_SAMPLES; ++i) {
        tx.seq = i;
        uint64_t t0 = rdtscp_now();
        while (!layout->cpp_to_rust.try_push(tx)) _mm_pause();
        while (!layout->rust_to_cpp.try_pop(rx))  _mm_pause();
        uint64_t t1 = rdtscp_now();
        hist.record(int64_t(t1 - t0));
    }

    // Tell the responder to exit, then drain its echo.
    tx.seq = STOP_SENTINEL;
    while (!layout->cpp_to_rust.try_push(tx)) _mm_pause();
    while (!layout->rust_to_cpp.try_pop(rx))  _mm_pause();

    hist.report("cpp pingpong RTT", ticks_per_ns);
    return 0;
}

static int run_responder(SHMLayout* layout, double /*ticks_per_ns*/) {
    pin_to_core(RESPONDER_CORE);
    maybe_sched_fifo("responder");

    Message m;
    for (;;) {
        while (!layout->cpp_to_rust.try_pop(m)) _mm_pause();
        // Echo first (including sentinel) so the initiator can drain cleanly,
        // then exit if it was the sentinel.
        while (!layout->rust_to_cpp.try_push(m)) _mm_pause();
        if (m.seq == STOP_SENTINEL) return 0;
    }
}

int main(int argc, char** argv) {
    if (argc < 3 || std::strcmp(argv[1], "--role") != 0 ||
        (std::strcmp(argv[2], "initiator") != 0 && std::strcmp(argv[2], "responder") != 0)) {
        std::fprintf(stderr, "usage: %s --role {initiator|responder}\n", argv[0]);
        return 2;
    }
    const bool is_initiator = (std::strcmp(argv[2], "initiator") == 0);
    const char* role = argv[2];

    std::printf("[cpp-%s] attaching to %s...\n", role, SHM_NAME);
    void* mem = shm_attach(SHM_NAME, sizeof(SHMLayout), true);
    auto* layout = static_cast<SHMLayout*>(mem);

    maybe_mlock(mem, sizeof(SHMLayout), role);

    std::printf("[cpp-%s] init_or_wait...\n", role);
    double ticks_per_ns = init_or_wait(layout);
    std::printf("[cpp-%s] ticks_per_ns = %.6f\n", role, ticks_per_ns);

    return is_initiator
        ? run_initiator(layout, ticks_per_ns)
        : run_responder(layout, ticks_per_ns);
}

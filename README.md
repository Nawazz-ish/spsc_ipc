# spsc_ipc

Lock-free single-producer / single-consumer ring buffer with an in-process
latency benchmark. Shared-memory IPC variant is work in progress.

## Layout

```
shared/include/
  spsc_queue.hpp   cache-line-padded SPSC ring (power-of-2 capacity)
  message.hpp      64-byte Message {tsc, seq, pad}
  tsc.hpp          rdtscp + CLOCK_MONOTONIC calibration
  histogram.hpp    sample buffer with p50/p90/p99/max report
  shm.hpp          (stub — shared-memory transport TODO)
cpp/bench_inprocess.cpp   producer/consumer pinned to cores 0/1
tools/tsc_calibrate.cpp   prints 5 TSC calibration runs
tests/test_compile.cpp    smoke compile check
```

## Build & run

```bash
g++ -O3 -std=c++20 -pthread cpp/bench_inprocess.cpp -o bench_inprocess
./bench_inprocess
```

Producer pins to core 0, consumer to core 1. 10k warmup + 1M measured
samples; latency is `rdtscp(consumer) - tsc(producer)` converted to ns.

## Notes

- `Capacity` must be a power of two; `T` must be trivially copyable and
  standard-layout.
- Head/tail are on separate cache lines with cached opposite-index to
  avoid cross-core ping-pong on the hot path.
- TSC calibration takes the median of 5 × 100 ms samples against
  `CLOCK_MONOTONIC`. Pin cores and disable turbo for stable numbers.

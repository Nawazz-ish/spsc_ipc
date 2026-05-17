# spsc_ipc

Lock-free single-producer / single-consumer ring buffer with cross-language
(C++ ↔ Rust) shared-memory IPC, benchmarked end-to-end on a tuned Linux box.

## Results

Ping-pong round-trip (RTT) over `/dev/shm` on bare-metal `c5.metal` (Xeon
Platinum 8124M, 3.0 GHz, isolated cores). 1M samples per run, 3 reps.

| Configuration                         | p50    | p99    | p99.9  | max      |
|---------------------------------------|--------|--------|--------|----------|
| Stock Linux (cores 0,1)               | 213 ns | 304 ns | 348 ns | 17.3 µs  |
| **Isolated cores (final)**            | **237 ns** | **293 ns** | **305 ns** | **491 ns** |

**Headline numbers (isolated, RTT/2 ≈ one-way):**

- One-way p50 ≈ **120 ns**
- One-way p99.9 ≈ **150 ns**
- One-way max ≈ **250 ns** over 1,000,000 samples

The four cross-language pairings (C++↔C++, C++↔Rust, Rust↔C++, Rust↔Rust) are
within ±10 ns of each other at p50 and ±5 ns at p99.9.

## Quick start

```bash
# C++ ping-pong
g++ -std=c++17 -O3 -pthread cpp/pingpong.cpp -o /tmp/pingpong_cpp -lrt

# Rust ping-pong
cargo build --release --manifest-path rust/Cargo.toml

# Run on isolated cores 2,3 (responder first, then initiator)
rm -f /dev/shm/shm_ipc
taskset -c 3 /tmp/pingpong_cpp --role responder &
sleep 0.5
taskset -c 2 /tmp/pingpong_cpp --role initiator
```

See [Reproducing](#reproducing) for the full tuned setup.

## Architecture

```
        ┌─────────────────────── /dev/shm/shm_ipc ───────────────────────┐
        │                                                                │
        │   ┌────────────────────────────┐    ┌──────────────────────┐  │
        │   │  cpp_to_rust : SpscQueue   │    │  init_state (atomic) │  │
        │   │  ─ producer cache line ──  │    │  ticks_per_ns_fixed  │  │
        │   │      head_, cached_tail_   │    └──────────────────────┘  │
        │   │  ─ consumer cache line ──  │                              │
        │   │      tail_, cached_head_   │    ┌──────────────────────┐  │
        │   │  ─ buffer[4096] ─────────  │    │  rust_to_cpp queue   │  │
        │   └────────────────────────────┘    └──────────────────────┘  │
        │                                                                │
        └────────────────────────────────────────────────────────────────┘
            ▲                                            ▲
            │ push/pop                                   │ push/pop
            │                                            │
        ┌───┴────────┐                              ┌────┴───────┐
        │ Initiator  │  pinned to core 2            │ Responder  │  pinned to core 3
        │ (any lang) │                              │ (any lang) │
        └────────────┘                              └────────────┘
```

Two SPSC queues — one per direction — sit in a POSIX shared-memory region
(`shm_open` + `mmap`). Whichever process attaches first calibrates the TSC and
publishes the rate into the layout; the other waits via a 3-state atomic.

Both languages share the **same byte layout** for `Message`, `SpscQueue`, and
`ShmLayout`. An ABI-check tool prints `size_of`/`offset_of` for both
implementations so layout drift gets caught immediately.

## Build prerequisites

- Linux x86_64 with `rdtscp` + `constant_tsc` + `nonstop_tsc` (any modern Intel/AMD)
- `g++` with C++17, `cargo` 1.74+
- `libcap2-bin` (for `setcap cap_sys_nice` if using SCHED_FIFO)
- `linux-tools-common` (for `turbostat` verification)
- root or passwordless sudo for boot-time kernel tuning

## Development phases

The project was built in six incremental phases. Each phase added one concrete
capability and was kept buildable on its own.

### Phase 1 — SPSC ring buffer

[shared/include/spsc_queue.hpp](shared/include/spsc_queue.hpp),
[shared/include/message.hpp](shared/include/message.hpp)

Template `SPSCQueue<T, Capacity>` with power-of-2 capacity and standard
single-producer/single-consumer invariants. Key design choices:

- **Head and tail on separate cache lines** via `alignas(64)`. Without this,
  the producer's `head_.store` would invalidate the consumer's cache line on
  every push (and vice versa) — true sharing through a single line costs ~50 ns
  of coherency traffic per operation.
- **Cached opposite index** (`cached_tail_` on the producer side,
  `cached_head_` on the consumer side). The fast path reads only the local
  cached value; the remote atomic is only consulted when the cached value
  predicts the queue is full/empty. This collapses the per-operation cache
  miss into one miss per ~capacity operations.
- **`Message` is exactly 64 bytes** (`tsc` + `seq` + 48-byte pad) so each slot
  occupies its own cache line and slots don't false-share.
- **Acquire/Release ordering** on the published index; relaxed on the local
  side. The relaxed local load is safe because only one thread/process writes
  it.

### Phase 2 — TSC timing + in-process benchmark

[shared/include/tsc.hpp](shared/include/tsc.hpp),
[shared/include/histogram.hpp](shared/include/histogram.hpp),
[cpp/bench_inprocess.cpp](cpp/bench_inprocess.cpp)

Producer thread on core 0 timestamps a message with `rdtscp`, pushes it;
consumer thread on core 1 pops, takes another `rdtscp`, records the delta.

TSC is calibrated once at startup as the median of 5 × 100 ms windows against
`CLOCK_MONOTONIC`. On x86 with `constant_tsc` + `nonstop_tsc` (verified at
boot), the TSC ticks at a fixed rate regardless of CPU frequency scaling.

The histogram is a pre-reserved `vector<int64_t>`. Two important hot-path
optimizations landed during tuning:

- **Record raw ticks, not nanoseconds.** Converting via `ticks/ticks_per_ns`
  in the hot loop is a float divide (~15 cycles); deferring it to report time
  drops consumer-side work to ~50 ns/iteration.
- **Pace the producer.** Without pacing, the producer outruns the consumer
  (because the consumer does one extra `rdtscp` per iteration), the queue
  saturates at depth ~4000, and the "latency" you measure is
  `depth × consumer_period` — about 6 ms. With a ~1 µs busy-wait between
  pushes the queue stays empty and you measure transit latency (~120 ns).

This second issue (the queue-saturation trap) is a textbook latency-bench
mistake and is the reason Phase 6 switched to ping-pong, which is mechanically
self-throttling.

### Phase 3 — Cross-process via POSIX shared memory

[shared/include/shm.hpp](shared/include/shm.hpp),
[shared/include/shm_layout.hpp](shared/include/shm_layout.hpp),
[cpp/producer.cpp](cpp/producer.cpp),
[cpp/consumer.cpp](cpp/consumer.cpp)

The C++ producer and consumer become **separate processes** sharing memory
via `shm_open("/shm_ipc")` + `mmap(MAP_SHARED)`. The `SHMLayout` struct in
`shared/include/shm_layout.hpp` puts:

- A 3-state `init_state` atomic + `ticks_per_ns_fixed` on the first cache line
- `cpp_to_rust : SpscQueue` on its own aligned region
- `rust_to_cpp : SpscQueue` on its own aligned region

The first process to win a `compare_exchange` on `init_state` runs TSC
calibration and publishes the rate; the other spins on `init_state` until
ready. This avoids needing an out-of-band rendezvous.

The relevant cross-process safety property: both processes get **different
virtual addresses** for the same physical pages, but the atomic operations on
`head_`/`tail_` are coherent because the underlying physical pages are shared
and x86 cache coherency is per-physical-line.

### Phase 4 — Rust port and ABI verification

[rust/shared-rs/](rust/shared-rs/), [rust/abi-check/src/main.rs](rust/abi-check/src/main.rs),
[cpp/abi_check.cpp](cpp/abi_check.cpp)

Rust reimplementation of every shared header:

| C++ header                | Rust module                                                                 |
|---------------------------|-----------------------------------------------------------------------------|
| `spsc_queue.hpp`          | [rust/shared-rs/src/spsc_queue.rs](rust/shared-rs/src/spsc_queue.rs)          |
| `message.hpp`             | [rust/shared-rs/src/message.rs](rust/shared-rs/src/message.rs)              |
| `tsc.hpp`                 | [rust/shared-rs/src/tsc.rs](rust/shared-rs/src/tsc.rs)                      |
| `shm.hpp`                 | [rust/shared-rs/src/shm.rs](rust/shared-rs/src/shm.rs)                      |
| `shm_layout.hpp`          | [rust/shared-rs/src/shm_layout.rs](rust/shared-rs/src/shm_layout.rs)        |
| `histogram.hpp`           | [rust/shared-rs/src/histogram.rs](rust/shared-rs/src/histogram.rs)          |

The ABI requirements that needed reproducing exactly:

- `Message`: `#[repr(C)]` to lock field order, 64-byte size enforced by a
  `const _: () = assert!(size_of::<Message>() == 64);`.
- `SpscQueue`: three separately-`#[repr(C, align(64))]` lines —
  `ProducerLine { head, cached_tail }`, `ConsumerLine { tail, cached_head }`,
  `BufferLine { data }` — laid out by an outer `#[repr(C)]` struct.
- `ShmLayout`: explicit padding fields (`_pad0`, `_pad1`) where the C++
  layout has implicit cache-line padding from `alignas(64)`.

`abi-check` (both languages) prints `size`, `align`, and every field offset.
The two outputs are diffed in CI; any drift surfaces immediately. The
`UnsafeCell` wrappers on `cached_tail` / `cached_head` / `data` are Rust's way
of saying "interior mutability through `&self`" — required because the queue
is shared via `&ShmLayout` (no `&mut` possible across processes).

### Phase 5 — Cross-language interop matrix (one-way)

Run the C++ producer with the Rust consumer (or any other combination), all
four cells. Result: indistinguishable at p50 (within ~20 ns), the four
implementations are wire-compatible. This is the practical proof that Phase 4
got the ABI right.

The one-way benchmark uses producer-rdtscp → consumer-rdtscp on different
cores. This *does* assume TSC sync across cores, which on bare-metal x86 is
guaranteed by `constant_tsc` + `nonstop_tsc` + the kernel's boot-time
sync check. On a virtualized EC2 instance you'd see a constant ~hundreds-of-µs
TSC offset per core; here the offset is zero.

### Phase 6a — Ping-pong (the proper latency benchmark)

[cpp/pingpong.cpp](cpp/pingpong.cpp),
[rust/pingpong/src/main.rs](rust/pingpong/src/main.rs)

Single binary per language with `--role initiator|responder`:

- **Initiator** captures `t0`, pushes to `cpp_to_rust`, spin-waits on a pop
  from `rust_to_cpp`, captures `t1`, records `t1 - t0` (RTT).
- **Responder** pops from `cpp_to_rust`, immediately pushes the same message
  back via `rust_to_cpp`. No timestamps.

Two consequences of this shape:

1. **Self-throttling.** The initiator can't push message N+1 until it gets
   back the response for N, so the queue depth is mechanically bounded at 1.
   No producer pacing needed; no queue-saturation trap.
2. **Single-clock measurement.** The initiator does both `rdtscp` calls, so
   the result is independent of any cross-core TSC offset. The half-RTT is a
   close upper bound on one-way latency (it overestimates by the responder's
   pop/push loop time, ~5-10 ns).

### Phase 6b — Kernel tuning

The pre-reboot baseline already sat at p50 ≈ 213 ns, p99.9 ≈ 348 ns. The
remaining tail (max ≈ 17 µs) is kernel preemption events: scheduler ticks,
RCU callbacks, kworkers.

| Knob tried                                      | p50 Δ | p99.9 Δ | max Δ      | Verdict   |
|-------------------------------------------------|-------|---------|------------|-----------|
| `mlock` of shm region                           | none  | none    | none       | placebo on a warm working set |
| `SCHED_FIFO 50` (without isolation)             | none  | none    | **worse**  | defers housekeeping into rarer-but-deeper stalls |
| `cpupower idle-set -D 0` (C-states off)         | none  | none    | none       | irrelevant on a busy-spinning core (already in C0) |
| `isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3`       | none  | **−12%** | **−97%**  | **the only knob that mattered** |
| `mitigations=off`                               | none  | none    | none       | hot path has no syscalls; doesn't show |

**Lessons:**

1. The single biggest tail-latency win was kernel-level core isolation. Stock
   kernel: max 17 µs. Isolated cores: max 491 ns.
2. SCHED_FIFO without isolation is a footgun. It blocks normal kernel
   housekeeping until the kernel forces a longer-than-usual preemption to
   catch up. On stock cores this *raises* `max` from ~17 µs to ~400 µs.
3. Once isolation is doing its job, *every other knob hurts a little or
   doesn't help.* The best ping-pong configuration on this box is:
   isolated cores + no SCHED_FIFO + no mlock.
4. **`nohz_full` cannot include the boot CPU (CPU 0).** The kernel silently
   drops it from the mask. Using cores 0,1 with `nohz_full=0,1` gives you
   `nohz_full=1` only — asymmetric, half the win. Use any non-boot pair
   (e.g. 2,3).

## Reproducing

### One-time kernel setup (requires reboot)

```bash
sudo tee /etc/default/grub.d/99-lowlatency.cfg > /dev/null <<'EOF'
GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 mitigations=off"
EOF
sudo update-grub
sudo reboot
```

Post-reboot verification:

```bash
cat /sys/devices/system/cpu/isolated     # → 2-3
cat /sys/devices/system/cpu/nohz_full    # → 2-3
```

### Build

```bash
g++ -std=c++17 -O3 -pthread cpp/pingpong.cpp -o /tmp/pingpong_cpp -lrt
cargo build --release --manifest-path rust/Cargo.toml
```

### Run any pairing

```bash
CPP=/tmp/pingpong_cpp
RUST=rust/target/release/pingpong

rm -f /dev/shm/shm_ipc
taskset -c 3 $CPP  --role responder &     # or $RUST
sleep 0.5
taskset -c 2 $RUST --role initiator       # or $CPP
```

### Verify ABI compatibility

```bash
g++ -std=c++17 -O3 cpp/abi_check.cpp -o /tmp/abi_check_cpp
diff <(/tmp/abi_check_cpp) <(rust/target/release/abi-check)
```

## Layout

```
shared/include/
  spsc_queue.hpp    cache-line-padded SPSC ring (Phase 1)
  message.hpp       64-byte Message {tsc, seq, pad} (Phase 1)
  tsc.hpp           rdtscp + CLOCK_MONOTONIC calibration (Phase 2)
  histogram.hpp     sample buffer with p50/p90/p99/p99.9/max (Phase 2)
  shm.hpp           shm_open + mmap wrapper (Phase 3)
  shm_layout.hpp    SHMLayout with two queues + init coordination (Phase 3)
cpp/
  bench_inprocess.cpp    threaded in-process bench (Phase 2)
  producer.cpp           cross-process producer (Phase 3)
  consumer.cpp           cross-process consumer (Phase 3)
  abi_check.cpp          prints C++ struct offsets (Phase 4)
  pingpong.cpp           round-trip benchmark + tuning knobs (Phase 6)
rust/
  shared-rs/             port of shared/include/ (Phase 4)
  abi-check/             prints Rust struct offsets (Phase 4)
  producer/, consumer/   cross-process bins (Phase 4)
  pingpong/              round-trip benchmark (Phase 6)
tests/, tools/           scaffolding
```

## Things that bit me along the way

- **Queue saturation looks like latency.** A 6 ms median in the one-way bench
  was the producer outpacing the consumer by ~20 ns/iter and the queue piling
  up to depth ~4000. Pacing or ping-pong fixes it.
- **Zombie consumers from killed runs.** When `pgrep` shows your bench binary
  with PSR=1 and pcpu=30%, you have stale spinners from previous Ctrl+C'd
  runs starving the real consumer. `pkill -9 -x consumer` before every run.
- **Asymmetric isolation gives asymmetric wins.** With `nohz_full=0,1` the
  kernel only honors `1`. With cores 0,1 isolated and pinned, p99.9 doesn't
  move. Use a non-boot core pair.
- **The compiler warning about `std::hardware_destructive_interference_size`
  is real for IPC.** Two TUs compiled with different `-mtune` flags would see
  different cache-line sizes, breaking layout. We hardcode 64.

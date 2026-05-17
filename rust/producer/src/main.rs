use std::hint::spin_loop;
use std::mem::size_of;

use shared_rs::message::Message;
use shared_rs::shm::shm_attach;
use shared_rs::shm_layout::{init_or_wait, ShmLayout, SHM_NAME};
use shared_rs::tsc::rdtscp_now;

const WARMUP_SAMPLES: usize = 10_000;
const MEASURE_SAMPLES: usize = 1_000_000;
const STOP_SENTINEL: u64 = u64::MAX;
const PRODUCER_CORE: usize = 0;

fn pin_to_core(core: usize) {
    unsafe {
        let mut set: libc::cpu_set_t = std::mem::zeroed();
        libc::CPU_ZERO(&mut set);
        libc::CPU_SET(core, &mut set);
        let rc = libc::sched_setaffinity(0, size_of::<libc::cpu_set_t>(), &set);
        if rc != 0 {
            eprintln!("[rust-producer] warning: failed to pin to core {}", core);
        }
    }
}

fn main() {
    println!("[rust-producer] attaching to {}...", SHM_NAME);
    let p = shm_attach(SHM_NAME, size_of::<ShmLayout>(), true);
    let layout: &ShmLayout = unsafe { &*(p as *const ShmLayout) };

    println!("[rust-producer] init_or_wait...");
    let ticks_per_ns = init_or_wait(layout);
    println!("[rust-producer] ticks_per_ns = {:.6}", ticks_per_ns);

    pin_to_core(PRODUCER_CORE);

    let pace_ticks = (1000.0 * ticks_per_ns) as u64; // ~1 µs of TSC ticks
    let mut m = Message::default();

    // Warmup (no pacing — let the queue prime).
    for i in 0..WARMUP_SAMPLES {
        m.tsc = rdtscp_now();
        m.seq = i as u64;
        while !layout.cpp_to_rust.try_push(m) {
            spin_loop();
        }
    }

    // Measured — pace ~1 µs between sends to keep queue drained.
    for i in 0..MEASURE_SAMPLES {
        m.tsc = rdtscp_now();
        m.seq = i as u64;
        while !layout.cpp_to_rust.try_push(m) {
            spin_loop();
        }

        let deadline = rdtscp_now() + pace_ticks;
        while rdtscp_now() < deadline {
            // spin
        }
    }

    // Sentinel.
    m.seq = STOP_SENTINEL;
    m.tsc = 0;
    while !layout.cpp_to_rust.try_push(m) {
        spin_loop();
    }

    println!(
        "[rust-producer] done. sent {} messages.",
        WARMUP_SAMPLES + MEASURE_SAMPLES + 1
    );
}

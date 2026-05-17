use std::hint::spin_loop;
use std::mem::size_of;

use shared_rs::histogram::Histogram;
use shared_rs::message::Message;
use shared_rs::shm::shm_attach;
use shared_rs::shm_layout::{init_or_wait, ShmLayout, SHM_NAME};
use shared_rs::tsc::rdtscp_now;

const WARMUP_SAMPLES: usize = 10_000;
const MEASURE_SAMPLES: usize = 1_000_000;
const STOP_SENTINEL: u64 = u64::MAX;
const CONSUMER_CORE: usize = 1;

fn pin_to_core(core: usize) {
    unsafe {
        let mut set: libc::cpu_set_t = std::mem::zeroed();
        libc::CPU_ZERO(&mut set);
        libc::CPU_SET(core, &mut set);
        let rc = libc::sched_setaffinity(0, size_of::<libc::cpu_set_t>(), &set);
        if rc != 0 {
            eprintln!("[rust-consumer] warning: failed to pin to core {}", core);
        }
    }
}

fn main() {
    println!("[rust-consumer] attaching to {}...", SHM_NAME);
    let p = shm_attach(SHM_NAME, size_of::<ShmLayout>(), true);

    // SAFETY: the bytes at `p` are a valid (possibly zeroed) ShmLayout.
    // Zero-initialized memory is a valid empty queue by design.
    let layout: &ShmLayout = unsafe { &*(p as *const ShmLayout) };

    println!("[rust-consumer] init_or_wait...");
    let ticks_per_ns = init_or_wait(layout);
    println!("[rust-consumer] ticks_per_ns = {:.6}", ticks_per_ns);

    pin_to_core(CONSUMER_CORE);

    let mut hist = Histogram::with_capacity(MEASURE_SAMPLES);
    let mut m = Message::default();

    // Warmup — pop and discard.
    for _ in 0..WARMUP_SAMPLES {
        while !layout.cpp_to_rust.try_pop(&mut m) {
            spin_loop();
        }
    }

    // Measure — record raw tick deltas.
    for _ in 0..MEASURE_SAMPLES {
        while !layout.cpp_to_rust.try_pop(&mut m) {
            spin_loop();
        }
        let now = rdtscp_now();
        hist.record((now - m.tsc) as i64);
    }

    // Wait for the sentinel and exit.
    loop {
        if layout.cpp_to_rust.try_pop(&mut m) && m.seq == STOP_SENTINEL {
            break;
        }
        spin_loop();
    }

    hist.report("cpp→rust one-way", ticks_per_ns);
}

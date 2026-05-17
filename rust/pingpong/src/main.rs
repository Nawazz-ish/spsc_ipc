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
const INITIATOR_CORE: usize = 2;
const RESPONDER_CORE: usize = 3;

fn pin_to_core(core: usize) {
    unsafe {
        let mut set: libc::cpu_set_t = std::mem::zeroed();
        libc::CPU_ZERO(&mut set);
        libc::CPU_SET(core, &mut set);
        let rc = libc::sched_setaffinity(0, size_of::<libc::cpu_set_t>(), &set);
        if rc != 0 {
            eprintln!("[rust-pingpong] warning: failed to pin to core {}", core);
        }
    }
}

fn run_initiator(layout: &ShmLayout, ticks_per_ns: f64) {
    pin_to_core(INITIATOR_CORE);

    let mut hist = Histogram::with_capacity(MEASURE_SAMPLES);
    let mut tx = Message::default();
    let mut rx = Message::default();

    // Warmup
    for i in 0..WARMUP_SAMPLES {
        tx.seq = i as u64;
        while !layout.cpp_to_rust.try_push(tx) { spin_loop(); }
        while !layout.rust_to_cpp.try_pop(&mut rx) { spin_loop(); }
    }

    // Measure — queue depth ≤ 1, self-throttling, no pacing.
    for i in 0..MEASURE_SAMPLES {
        tx.seq = i as u64;
        let t0 = rdtscp_now();
        while !layout.cpp_to_rust.try_push(tx) { spin_loop(); }
        while !layout.rust_to_cpp.try_pop(&mut rx) { spin_loop(); }
        let t1 = rdtscp_now();
        hist.record((t1 - t0) as i64);
    }

    tx.seq = STOP_SENTINEL;
    while !layout.cpp_to_rust.try_push(tx) { spin_loop(); }
    while !layout.rust_to_cpp.try_pop(&mut rx) { spin_loop(); }

    hist.report("rust pingpong RTT", ticks_per_ns);
}

fn run_responder(layout: &ShmLayout) {
    pin_to_core(RESPONDER_CORE);

    let mut m = Message::default();
    loop {
        while !layout.cpp_to_rust.try_pop(&mut m) { spin_loop(); }
        while !layout.rust_to_cpp.try_push(m) { spin_loop(); }
        if m.seq == STOP_SENTINEL { return; }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let role = match args.as_slice() {
        [_, flag, role] if flag == "--role" && (role == "initiator" || role == "responder") => role.clone(),
        _ => {
            eprintln!("usage: {} --role {{initiator|responder}}", args[0]);
            std::process::exit(2);
        }
    };

    println!("[rust-{}] attaching to {}...", role, SHM_NAME);
    let p = shm_attach(SHM_NAME, size_of::<ShmLayout>(), true);
    let layout: &ShmLayout = unsafe { &*(p as *const ShmLayout) };

    println!("[rust-{}] init_or_wait...", role);
    let ticks_per_ns = init_or_wait(layout);
    println!("[rust-{}] ticks_per_ns = {:.6}", role, ticks_per_ns);

    if role == "initiator" {
        run_initiator(layout, ticks_per_ns);
    } else {
        run_responder(layout);
    }
}

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};

use crate::message::Message;
use crate::spsc_queue::SpscQueue;
use crate::tsc::calibrate_tsc;

pub const SHM_QUEUE_CAPACITY: usize = 4096;
pub const INIT_UNINIT: u32 = 0;
pub const INIT_INPROGRESS: u32 = 1;
pub const INIT_READY: u32 = 2;
pub const TICKS_PER_NS_SCALE: u64 = 1_000_000_000;
pub const SHM_NAME: &str = "/shm_ipc";

#[repr(C, align(64))]
pub struct ShmLayout {
    pub init_state: AtomicU32,
    _pad0: [u8; 4],
    pub ticks_per_ns_fixed: AtomicU64,
    _pad1: [u8; 48],

    pub cpp_to_rust: SpscQueue<Message, SHM_QUEUE_CAPACITY>,
    pub rust_to_cpp: SpscQueue<Message, SHM_QUEUE_CAPACITY>,
}

pub fn init_or_wait(layout: &ShmLayout) -> f64 {
    let mut expected = INIT_UNINIT;
    match layout.init_state.compare_exchange(
        expected,
        INIT_INPROGRESS,
        Ordering::Acquire,
        Ordering::Acquire,
    ) {
        Ok(_) => {
            // We won the race — calibrate and publish.
            let cal = calibrate_tsc();
            let fixed = (cal.ticks_per_ns * TICKS_PER_NS_SCALE as f64) as u64;
            layout.ticks_per_ns_fixed.store(fixed, Ordering::Relaxed);
            layout.init_state.store(INIT_READY, Ordering::Release);
            cal.ticks_per_ns
        }
        Err(_) => {
            // Lost the race — wait for ready, then read calibration.
            while layout.init_state.load(Ordering::Acquire) != INIT_READY {
                std::hint::spin_loop();
            }
            let fixed = layout.ticks_per_ns_fixed.load(Ordering::Relaxed);
            (fixed as f64) / (TICKS_PER_NS_SCALE as f64)
        }
    }
}

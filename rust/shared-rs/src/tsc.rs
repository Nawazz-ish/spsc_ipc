use std::arch::x86_64::__rdtscp;
use std::time::Instant;

#[inline(always)]
pub fn rdtscp_now() -> u64 {
    let mut aux: u32 = 0;
    // SAFETY: __rdtscp is a CPU instruction that has no side effects on
    // program memory. The `aux` out-parameter receives the processor ID
    // which we ignore.
    unsafe { __rdtscp(&mut aux) }
}

pub struct TscCalibration {
    pub ticks_per_ns: f64,
}

pub fn calibrate_tsc() -> TscCalibration {
    const SAMPLES: usize = 5;
    const SLEEP_MS: u64 = 100;

    let mut rates = Vec::with_capacity(SAMPLES);
    for _ in 0..SAMPLES {
        let wall_start = Instant::now();
        let tsc_start = rdtscp_now();
        std::thread::sleep(std::time::Duration::from_millis(SLEEP_MS));
        let tsc_end = rdtscp_now();
        let wall_end = Instant::now();

        let ns_elapsed = wall_end.duration_since(wall_start).as_nanos() as f64;
        let ticks_elapsed = (tsc_end - tsc_start) as f64;
        rates.push(ticks_elapsed / ns_elapsed);
    }
    rates.sort_by(|a, b| a.partial_cmp(b).unwrap());
    TscCalibration { ticks_per_ns: rates[rates.len() / 2] }
}

#[inline(always)]
pub fn ticks_to_ns(ticks: u64, ticks_per_ns: f64) -> i64 {
    (ticks as f64 / ticks_per_ns) as i64
}

pub struct Histogram {
    samples: Vec<i64>,
}

impl Histogram {
    pub fn new() -> Self {
        Self { samples: Vec::new() }
    }

    pub fn with_capacity(cap: usize) -> Self {
        Self { samples: Vec::with_capacity(cap) }
    }

    #[inline(always)]
    pub fn record(&mut self, value: i64) {
        self.samples.push(value);
    }

    pub fn len(&self) -> usize {
        self.samples.len()
    }

    pub fn report(&mut self, label: &str, scale_to_ns: f64) {
        if self.samples.is_empty() {
            println!("{:<20} (empty)", label);
            return;
        }
        self.samples.sort_unstable();
        let pct = |p: f64| -> i64 {
            self.samples[(self.samples.len() as f64 * p) as usize]
        };
        let to_ns = |ticks: i64| -> f64 { ticks as f64 / scale_to_ns };
        println!(
            "{:<20} n={}  p50={:.2}  p90={:.2}  p99={:.2}  p99.9={:.2}  max={:.2}  (ns)",
            label,
            self.samples.len(),
            to_ns(pct(0.50)),
            to_ns(pct(0.90)),
            to_ns(pct(0.99)),
            to_ns(pct(0.999)),
            to_ns(*self.samples.last().unwrap()),
        );
    }
}

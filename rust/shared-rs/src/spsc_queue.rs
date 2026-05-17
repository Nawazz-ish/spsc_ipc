use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicUsize, Ordering};

#[repr(C, align(64))]
struct ProducerLine {
    head: AtomicUsize,
    cached_tail: UnsafeCell<usize>,
}

#[repr(C, align(64))]
struct ConsumerLine {
    tail: AtomicUsize,
    cached_head: UnsafeCell<usize>,
}

#[repr(C, align(64))]
struct BufferLine<T, const N: usize> {
    data: UnsafeCell<[T; N]>,
}

#[repr(C)]
pub struct SpscQueue<T: Copy, const N: usize> {
    producer: ProducerLine,
    consumer: ConsumerLine,
    buffer: BufferLine<T, N>,
}

// SAFETY: The contract is that exactly one thread/process pushes (touches
// producer.head + producer.cached_tail + buffer.data via head) and exactly
// one thread/process pops (touches consumer.tail + consumer.cached_head +
// buffer.data via tail). The atomics on head/tail synchronize the bounded
// regions of the buffer each side accesses. UnsafeCell is needed because
// cached_tail / cached_head / data are mutated through &self.
unsafe impl<T: Copy, const N: usize> Sync for SpscQueue<T, N> {}

impl<T: Copy, const N: usize> SpscQueue<T, N> {
    const MASK: usize = N - 1;

    pub fn try_push(&self, item: T) -> bool {
        debug_assert!(N.is_power_of_two() && N >= 2);

        let head = self.producer.head.load(Ordering::Relaxed);
        let next = (head + 1) & Self::MASK;

        // SAFETY: only the producer touches cached_tail.
        let cached = unsafe { *self.producer.cached_tail.get() };

        if next == cached {
            let real = self.consumer.tail.load(Ordering::Acquire);
            unsafe { *self.producer.cached_tail.get() = real; }
            if next == real {
                return false;
            }
        }

        // SAFETY: only the producer writes to buffer[head] before publishing
        // the new head via Release; the consumer won't read this slot until
        // it observes the new head via Acquire.
        unsafe {
            let buf = &mut *self.buffer.data.get();
            buf[head] = item;
        }
        self.producer.head.store(next, Ordering::Release);
        true
    }

    pub fn try_pop(&self, item: &mut T) -> bool {
        debug_assert!(N.is_power_of_two() && N >= 2);

        let tail = self.consumer.tail.load(Ordering::Relaxed);
        let cached = unsafe { *self.consumer.cached_head.get() };

        if tail == cached {
            let real = self.producer.head.load(Ordering::Acquire);
            unsafe { *self.consumer.cached_head.get() = real; }
            if tail == real {
                return false;
            }
        }

        // SAFETY: producer has published head via Release; we observed via
        // Acquire; the slot at `tail` is therefore safe to read.
        unsafe {
            let buf = &*self.buffer.data.get();
            *item = buf[tail];
        }
        self.consumer.tail.store((tail + 1) & Self::MASK, Ordering::Release);
        true
    }
}

use std::mem::{align_of, offset_of, size_of};

use shared_rs::message::Message;
use shared_rs::shm_layout::{ShmLayout, SHM_QUEUE_CAPACITY};
use shared_rs::spsc_queue::SpscQueue;

fn main() {
    println!("=== Rust ABI ===");
    println!(
        "Message: size={} align={}",
        size_of::<Message>(),
        align_of::<Message>()
    );
    println!("  tsc: offset={}", offset_of!(Message, tsc));
    println!("  seq: offset={}", offset_of!(Message, seq));
    println!("  pad: offset={}", offset_of!(Message, pad));

    type Q = SpscQueue<Message, SHM_QUEUE_CAPACITY>;
    println!(
        "\nSpscQueue<Message,{}>: size={} align={}",
        SHM_QUEUE_CAPACITY,
        size_of::<Q>(),
        align_of::<Q>()
    );

    println!(
        "\nShmLayout: size={} align={}",
        size_of::<ShmLayout>(),
        align_of::<ShmLayout>()
    );
    println!("  init_state:          offset={}", offset_of!(ShmLayout, init_state));
    println!("  ticks_per_ns_fixed:  offset={}", offset_of!(ShmLayout, ticks_per_ns_fixed));
    println!("  cpp_to_rust:         offset={}", offset_of!(ShmLayout, cpp_to_rust));
    println!("  rust_to_cpp:         offset={}", offset_of!(ShmLayout, rust_to_cpp));
}

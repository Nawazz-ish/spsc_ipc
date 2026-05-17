#include "../shared/include/shm_layout.hpp"
#include "../shared/include/message.hpp"
#include "../shared/include/spsc_queue.hpp"
#include <cstddef>
#include <cstdio>

int main() {
    printf("=== C++ ABI ===\n");
    printf("Message: size=%zu align=%zu\n", sizeof(Message), alignof(Message));
    printf("  tsc: offset=%zu\n", offsetof(Message, tsc));
    printf("  seq: offset=%zu\n", offsetof(Message, seq));
    printf("  pad: offset=%zu\n", offsetof(Message, pad));

    using Q = SPSCQueue<Message, SHM_QUEUE_CAPACITY>;
    printf("\nSpscQueue<Message,%zu>: size=%zu align=%zu\n",
           SHM_QUEUE_CAPACITY, sizeof(Q), alignof(Q));

    printf("\nShmLayout: size=%zu align=%zu\n", sizeof(SHMLayout), alignof(SHMLayout));
    printf("  init_state:          offset=%zu\n", offsetof(SHMLayout, init_state));
    printf("  ticks_per_ns_fixed:  offset=%zu\n", offsetof(SHMLayout, ticks_per_ns_fixed));
    printf("  cpp_to_rust:         offset=%zu\n", offsetof(SHMLayout, cpp_to_rust));
    printf("  rust_to_cpp:         offset=%zu\n", offsetof(SHMLayout, rust_to_cpp));

    return 0;
}
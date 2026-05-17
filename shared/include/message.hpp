#pragma once

#include <cstdint>
#include <type_traits>

struct Message{
    uint64_t tsc;// Timestamp counter value at send time, used for latency measurement by producer. 8 bytes = 64 bits
    uint64_t seq; //sequence number, can be used to verify message ordering and detect lost messages. 8 bytes = 64 bits
    uint8_t pad[48]; // uint8_t each has 1 byte = 8 bits, so 48 bytes = 384 bits. This padding ensures that the total size of the Message struct is 64 bytes, which is a common cache line size. Aligning the struct to a cache line can improve performance by reducing false sharing and ensuring that each message fits within a single cache line.
};

static_assert(sizeof(Message) == 64, "Message struct must be 64 bytes (one cache line)");

static_assert(std::is_trivially_copyable_v<Message>, "Message must be trivially copyable");
static_assert(std::is_standard_layout_v<Message>, "Message must be standard layout");
#pragma once
#include <array>
#include <atomic>
#include <new>
#include <type_traits>
#include <cstddef>

#if defined(__cpp_lib_hardware_interference_size)
    constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
#else
    constexpr std::size_t cache_line_size = 64; // Common cache line size
#endif


template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity&(Capacity-1))==0, "Capacity must be a power of 2");
    static_assert(Capacity>=2, "Capacity must be at least 2");

    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must be standard layout");

    static_assert(sizeof(std::atomic<size_t>)==sizeof(size_t), "Atomic size mismatch");
    static_assert(std::atomic<size_t>::is_always_lock_free, "atomic size_t must be lock-free");


    private:
        static constexpr size_t index_mask = Capacity-1;
        alignas(cache_line_size) std::atomic<size_t> head_{0};
        size_t cached_tail_{0};


        alignas(cache_line_size) std::atomic<size_t> tail_{0};
        size_t cached_head_{0};

        alignas(cache_line_size) std::array<T, Capacity> buffer_;

    public:
    //default constructor and destructor for the SPSCQueue class, allowing for the creation and destruction of queue instances without any special initialization or cleanup logic.
    SPSCQueue() = default;
    ~SPSCQueue() = default;


    //banning the copy and move constructors and assignment operators to prevent unintended copying or moving of the queue

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator = (const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator = (SPSCQueue&&) = delete;

    bool try_push(const T& item){
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next_head = (head+1)&index_mask;


        //we are checking only when the head is about to wrap around and potentially overwrite the tail, which is a critical point for ensuring that we do not lose data in the queue. By checking if the next head position is equal to the cached tail, we can determine if the queue is full and needs to update the cached tail value before proceeding with the push operation.

        if(next_head == cached_tail_){
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if(next_head == cached_tail_){
                return false;
            }
        }
        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item){
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if(tail==cached_head_){
            cached_head_ = head_.load(std::memory_order_acquire);
            if(tail==cached_head_){
                return false;
            }
        }
        item = std::move(buffer_[tail]);
        // buffer_[tail].reset();
        tail_.store((tail+1)&index_mask, std::memory_order_release);
        return true;
    }

    //Utility

    size_t size_approx() const{
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return (h-t)&index_mask;
    }
    bool empty() const{
        return size_approx()==0;
    }
    static constexpr size_t capacity(){
        return Capacity-1;
    }

};
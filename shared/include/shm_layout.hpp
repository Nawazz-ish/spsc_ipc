#pragma once

#include "spsc_queue.hpp"
#include "message.hpp"
#include "tsc.hpp"
#include <atomic>
#include <cstdint>
#include <type_traits>


constexpr size_t SHM_QUEUE_CAPACITY = 4096;
constexpr size_t INIT_UNINIT = 0;
constexpr size_t INIT_INPROGRESS = 1;
constexpr size_t INIT_READY = 2;
constexpr uint64_t TICKS_PER_NS_SCALE = 1'000'000'000; // to be calibrated at runtime

constexpr const char* SHM_NAME = "/shm_ipc";


struct SHMLayout {
    //init coordination +calibration on thier own cache line
    alignas(64) std::atomic<uint32_t> init_state;
    std::atomic<uint64_t> ticks_per_ns_fixed;

    //2 queues for full duplex communication, each on its own cache line
    alignas(64) SPSCQueue<Message, SHM_QUEUE_CAPACITY> cpp_to_rust;
    alignas(64) SPSCQueue<Message, SHM_QUEUE_CAPACITY> rust_to_cpp;
};

static_assert(std::is_standard_layout_v<SHMLayout>);

//whoever wins the cas calibrates; loser waits for ready
inline double init_or_wait(SHMLayout* layout){
    uint32_t expected = INIT_UNINIT;
    if(layout->init_state.compare_exchange_strong(expected, INIT_INPROGRESS)){
        //i win, do the calibration
        auto cal = calibrate_tsc();
        uint64_t ticks_per_ns = uint64_t(cal.ticks_per_ns * TICKS_PER_NS_SCALE);
        layout->ticks_per_ns_fixed.store(ticks_per_ns * TICKS_PER_NS_SCALE), std::memory_order_release;
        layout->init_state.store(INIT_READY, std::memory_order_release);
        return ticks_per_ns;
    }else{
        while(layout->init_state.load(std::memory_order_acquire) != INIT_READY){
            //spin until ready
        }
        uint64_t fixed = layout->ticks_per_ns_fixed.load(std::memory_order_relaxed);
        return double(fixed) / TICKS_PER_NS_SCALE;
    }
    
}
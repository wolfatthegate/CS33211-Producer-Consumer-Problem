/* https://github.com/wolfatthegate/CS33211-Assignment
 * Author - Waylon Luo
 * C++ version - October 17, 2026
 *
 * Everything the producer and the consumer have to agree on: the names of
 * the kernel objects they meet at, the tunables, the layout of the shared
 * segment, and the hand-shake / clean-up rules.
 */

#ifndef PCP_PROTOCOL_HPP
#define PCP_PROTOCOL_HPP

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "ipc.hpp"

namespace pcp {

// ----------------------------------------------------------------- names
inline constexpr const char* kShmName      = "/pcp-sharemem";
inline constexpr const char* kSemEmptyName = "/pcp-sem-empty";
inline constexpr const char* kSemFullName  = "/pcp-sem-full";

// -------------------------------------------------------------- tunables
// Same meaning as BUFFER / THREAD_NUM / ITR in the C version.
inline constexpr int kBufferSize = 2;  // slots in the buffer
inline constexpr int kThreadNum  = 1;  // worker threads per process
inline constexpr int kIterations = 5;  // items each thread produces / consumes

inline constexpr std::chrono::seconds kProducerDelay{1};
inline constexpr std::chrono::seconds kConsumerDelay{2};

// How long the consumer keeps looking for the producer before giving up.
inline constexpr std::chrono::seconds kAttachTimeout{10};

// ------------------------------------------------------- shared segment
// Constructed in place by the producer (placement-new) and only *mapped* by
// the consumer. The two semaphores are named objects (see ipc.hpp), so they
// do not live in the segment the way they did in the C version.
struct ShmData {
    int x = 0;                       // id of the last item in the "stack"
    std::atomic<bool> ready{false};  // producer sets this once init is complete
    std::atomic<int>  attached{0};   // processes currently mapped; last one out unlinks
    SharedMutex       mutexBuffer;   // guards x; initialised by the producer
};

// Atomics in shared memory only work across processes if the hardware does
// them without a hidden lock.
static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> must be lock-free");
static_assert(std::atomic<int>::is_always_lock_free,  "std::atomic<int> must be lock-free");

// --------------------------------------------------------------- helpers

// Consumer side: block until the producer has finished initialising the
// segment (mutex created, semaphores created, counters set).
inline void waitUntilReady(const ShmData& data, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!data.ready.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("timed out waiting for the producer to initialise shared memory");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Both sides: called once on the way out. The *last* process to detach
// removes the named objects, so nothing is left behind no matter which
// process finishes first.
inline void detach(ShmData& data) {
    if (data.attached.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        data.mutexBuffer.destroy();
        SharedMemory<ShmData>::unlink(kShmName);
        NamedSemaphore::unlink(kSemEmptyName);
        NamedSemaphore::unlink(kSemFullName);
    }
}

}  // namespace pcp

#endif  // PCP_PROTOCOL_HPP

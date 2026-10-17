/* https://github.com/wolfatthegate/CS33211-Assignment
 * Author - Waylon Luo
 * C++ version - October 17, 2026
 *
 * Producer process. Creates the shared segment and the two semaphores,
 * then runs kThreadNum producer threads that each add kIterations items.
 */

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#include "protocol.hpp"

using namespace pcp;

// One producer thread.
void producer(ShmData& shm, NamedSemaphore& semEmpty, NamedSemaphore& semFull) {
    for (int i = 0; i < kIterations; ++i) {
        std::this_thread::sleep_for(kProducerDelay);

        semEmpty.wait();  // block while the buffer is full
        {
            std::lock_guard<SharedMutex> lock(shm.mutexBuffer);
            ++shm.x;  // critical section: produce one item
            std::cout << "Producer produces item " << shm.x << std::endl;
        }
        semFull.post();   // one more item is available
    }
}

int main() try {
    // 1. Create the segment and build the shared struct inside it.
    auto shm = SharedMemory<ShmData>::create(kShmName);
    ShmData* data = new (shm.get()) ShmData{};
    data->mutexBuffer.init();

    // 2. Create the semaphores: kBufferSize empty slots, zero full slots.
    NamedSemaphore semEmpty(kSemEmptyName, NamedSemaphore::Mode::Create, kBufferSize);
    NamedSemaphore semFull(kSemFullName, NamedSemaphore::Mode::Create, 0);

    // 3. Publish. Everything above must be visible before ready becomes true.
    data->attached.store(1, std::memory_order_relaxed);
    data->ready.store(true, std::memory_order_release);

    // 4. Run the worker threads.
    std::vector<std::thread> threads;
    threads.reserve(kThreadNum);
    for (int i = 0; i < kThreadNum; ++i) {
        std::cout << "producer thread created.." << std::endl;
        threads.emplace_back(producer, std::ref(*data), std::ref(semEmpty), std::ref(semFull));
    }
    for (auto& t : threads) t.join();

    // 5. Leave. If the consumer is already gone this also removes the names.
    detach(*data);
    return EXIT_SUCCESS;
} catch (const std::exception& e) {
    std::cerr << "producer: " << e.what() << '\n'
              << "producer: if objects were left over from an earlier run, run ./cleanup\n";
    return EXIT_FAILURE;
}

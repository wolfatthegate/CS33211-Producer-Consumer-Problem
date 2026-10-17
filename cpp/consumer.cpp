/* https://github.com/wolfatthegate/CS33211-Assignment
 * Author - Waylon Luo
 * C++ version - October 17, 2026
 *
 * Consumer process. Attaches to the segment the producer created, waits for
 * it to be initialised, then runs kThreadNum consumer threads that each
 * remove kIterations items.
 */

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "protocol.hpp"

using namespace pcp;

// One consumer thread.
void consumer(ShmData& shm, NamedSemaphore& semEmpty, NamedSemaphore& semFull) {
    for (int i = 0; i < kIterations; ++i) {
        std::this_thread::sleep_for(kConsumerDelay);

        semFull.wait();   // block while the buffer is empty
        {
            std::lock_guard<SharedMutex> lock(shm.mutexBuffer);
            std::cout << "Consumer consumes item " << shm.x << std::endl;
            --shm.x;  // critical section: consume one item
        }
        semEmpty.post();  // one more slot is free
    }
}

int main() try {
    // 1. Find the segment (retrying until the producer has created and sized
    //    it) and wait until the producer says it is initialised.
    auto shm = SharedMemory<ShmData>::attach(kShmName, kAttachTimeout);
    waitUntilReady(*shm, kAttachTimeout);
    shm->attached.fetch_add(1, std::memory_order_acq_rel);

    // 2. Open the semaphores the producer created.
    NamedSemaphore semEmpty(kSemEmptyName, NamedSemaphore::Mode::Open);
    NamedSemaphore semFull(kSemFullName, NamedSemaphore::Mode::Open);

    // 3. Run the worker threads.
    std::vector<std::thread> threads;
    threads.reserve(kThreadNum);
    for (int i = 0; i < kThreadNum; ++i) {
        std::cout << "consumer thread created.." << std::endl;
        threads.emplace_back(consumer, std::ref(*shm), std::ref(semEmpty), std::ref(semFull));
    }
    for (auto& t : threads) t.join();

    // 4. Leave. If the producer is already gone this also removes the names.
    detach(*shm);
    return EXIT_SUCCESS;
} catch (const std::exception& e) {
    std::cerr << "consumer: " << e.what() << '\n'
              << "consumer: is the producer running?\n";
    return EXIT_FAILURE;
}

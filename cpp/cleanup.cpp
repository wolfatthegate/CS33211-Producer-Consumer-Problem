/* https://github.com/wolfatthegate/CS33211-Assignment
 * Author - Waylon Luo
 * C++ version - October 17, 2026
 *
 * Removes the shared-memory segment and the two named semaphores if a
 * crashed run left them behind. Safe to run at any time.
 */

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "protocol.hpp"

using namespace pcp;

namespace {

void report(const char* kind, const char* name, bool removed) {
    if (removed) {
        std::cout << "removed " << kind << ' ' << name << '\n';
    } else if (errno == ENOENT) {
        std::cout << "no " << kind << ' ' << name << " to remove\n";
    } else {
        std::cerr << "could not remove " << kind << ' ' << name << ": " << std::strerror(errno) << '\n';
    }
}

}  // namespace

int main() {
    errno = 0;
    report("shared memory", kShmName, SharedMemory<ShmData>::unlink(kShmName));
    errno = 0;
    report("semaphore", kSemEmptyName, NamedSemaphore::unlink(kSemEmptyName));
    errno = 0;
    report("semaphore", kSemFullName, NamedSemaphore::unlink(kSemFullName));
    return EXIT_SUCCESS;
}

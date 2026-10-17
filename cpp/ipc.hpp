/* https://github.com/wolfatthegate/CS33211-Assignment
 * Author - Waylon Luo
 * C++ version - October 17, 2026
 *
 * Small RAII wrappers around the POSIX IPC primitives used by the
 * producer / consumer programs:
 *
 *   SharedMutex      - pthread mutex marked PTHREAD_PROCESS_SHARED, usable
 *                      with std::lock_guard
 *   NamedSemaphore   - sem_open() / sem_close() / sem_unlink()
 *   SharedMemory<T>  - shm_open() + ftruncate() + mmap() of exactly sizeof(T)
 *
 * Every failure is reported as std::system_error carrying errno, so main()
 * only needs one catch block.
 */

#ifndef PCP_IPC_HPP
#define PCP_IPC_HPP

#include <cerrno>
#include <chrono>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pcp {

[[noreturn]] inline void throwErrno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

[[noreturn]] inline void throwCode(int code, const std::string& what) {
    throw std::system_error(code, std::generic_category(), what);
}

// ------------------------------------------------------------------ SharedMutex
// A pthread mutex that lives inside a shared-memory segment. It has no
// constructor/destructor on purpose: the process that creates the segment
// calls init() once, and the last process to leave calls destroy().
// lock()/unlock() make it BasicLockable, so std::lock_guard<SharedMutex> works.
class SharedMutex {
public:
    void init() {
        pthread_mutexattr_t attr;
        int rc = pthread_mutexattr_init(&attr);
        if (rc != 0) throwCode(rc, "pthread_mutexattr_init");
        rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        if (rc != 0) {
            pthread_mutexattr_destroy(&attr);
            throwCode(rc, "pthread_mutexattr_setpshared");
        }
        rc = pthread_mutex_init(&mutex_, &attr);
        pthread_mutexattr_destroy(&attr);
        if (rc != 0) throwCode(rc, "pthread_mutex_init");
    }

    void destroy() noexcept { pthread_mutex_destroy(&mutex_); }

    void lock() {
        int rc = pthread_mutex_lock(&mutex_);
        if (rc != 0) throwCode(rc, "pthread_mutex_lock");
    }

    void unlock() noexcept { pthread_mutex_unlock(&mutex_); }

private:
    pthread_mutex_t mutex_;
};

// --------------------------------------------------------------- NamedSemaphore
// Named semaphores are used instead of sem_init() because
//   (a) they are visible to any process that knows the name, and
//   (b) macOS does not implement unnamed (sem_init) semaphores at all.
class NamedSemaphore {
public:
    enum class Mode { Create, Open };

    NamedSemaphore(const char* name, Mode mode, unsigned initialValue = 0)
        : name_(name) {
        if (mode == Mode::Create) {
            sem_ = sem_open(name, O_CREAT | O_EXCL, 0600, initialValue);
        } else {
            sem_ = sem_open(name, 0);
        }
        if (sem_ == SEM_FAILED) throwErrno(std::string("sem_open(") + name + ")");
    }

    ~NamedSemaphore() {
        if (sem_ != SEM_FAILED) sem_close(sem_);
    }

    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;

    // Decrement, blocking while the count is zero. Restarts if interrupted by a signal.
    void wait() {
        while (sem_wait(sem_) == -1) {
            if (errno != EINTR) throwErrno(std::string("sem_wait(") + name_ + ")");
        }
    }

    // Increment and wake one waiter, if any.
    void post() {
        if (sem_post(sem_) == -1) throwErrno(std::string("sem_post(") + name_ + ")");
    }

    const char* name() const noexcept { return name_; }

    // Remove the name from the system. Open handles keep working until closed.
    static bool unlink(const char* name) noexcept { return sem_unlink(name) == 0; }

private:
    const char* name_;
    sem_t* sem_ = SEM_FAILED;
};

// --------------------------------------------------------------- SharedMemory<T>
// Owns one shm_open() descriptor and one mapping of exactly sizeof(T) bytes.
// The mapping is released by the destructor; the *name* is only removed by an
// explicit unlink(), so the two processes can decide who does that.
template <typename T>
class SharedMemory {
public:
    // Create a brand-new segment sized for one T. Fails with EEXIST if a
    // segment of that name is already there (for example after a crash).
    static SharedMemory create(const char* name) {
        int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) throwErrno(std::string("shm_open(create ") + name + ")");
        if (ftruncate(fd, static_cast<off_t>(sizeof(T))) < 0) {
            int saved = errno;
            close(fd);
            shm_unlink(name);
            errno = saved;
            throwErrno(std::string("ftruncate(") + name + ")");
        }
        return SharedMemory(name, fd);
    }

    // Attach to a segment another process created. Keeps retrying until the
    // segment exists *and* has been sized, or until the timeout expires.
    static SharedMemory attach(const char* name, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            int fd = shm_open(name, O_RDWR, 0600);
            if (fd >= 0) {
                struct stat st {};
                if (fstat(fd, &st) == 0 && st.st_size >= static_cast<off_t>(sizeof(T))) {
                    return SharedMemory(name, fd);
                }
                close(fd);  // exists but ftruncate() has not run yet - keep waiting
            } else if (errno != ENOENT) {
                throwErrno(std::string("shm_open(attach ") + name + ")");
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                errno = ETIMEDOUT;
                throwErrno(std::string("waiting for ") + name);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    ~SharedMemory() {
        if (ptr_ != nullptr) munmap(ptr_, sizeof(T));
        if (fd_ >= 0) close(fd_);
    }

    SharedMemory(SharedMemory&& other) noexcept
        : name_(other.name_), fd_(other.fd_), ptr_(other.ptr_) {
        other.fd_ = -1;
        other.ptr_ = nullptr;
    }

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    SharedMemory& operator=(SharedMemory&&) = delete;

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    const char* name() const noexcept { return name_; }

    // Remove the name from the system. Existing mappings stay valid.
    static bool unlink(const char* name) noexcept { return shm_unlink(name) == 0; }

private:
    SharedMemory(const char* name, int fd) : name_(name), fd_(fd) {
        void* p = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            int saved = errno;
            close(fd);
            errno = saved;
            throwErrno(std::string("mmap(") + name + ")");
        }
        ptr_ = static_cast<T*>(p);
    }

    const char* name_;
    int fd_ = -1;
    T* ptr_ = nullptr;
};

}  // namespace pcp

#endif  // PCP_IPC_HPP

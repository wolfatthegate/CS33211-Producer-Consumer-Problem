# Producer-Consumer Problem (C++ version, 2026)

Author - Waylon Luo (Kent State University)

This is a C++17 rewrite of the [2021 C assignment](../c/README.md). It keeps the same
design (a producer process and a consumer process that share one counter protected
by two counting semaphores and a mutex) and produces the same output, but it fixes
the correctness problems found in the C code and runs on both Linux and macOS.

## Files

| File | Purpose |
|------|---------|
| `ipc.hpp` | RAII wrappers around the POSIX primitives: `SharedMemory<T>`, `NamedSemaphore`, `SharedMutex`. |
| `protocol.hpp` | What both processes agree on: object names, tunables, the `ShmData` layout, the start-up hand-shake and the clean-up rule. |
| `producer.cpp` | Creates the shared objects and runs the producer threads. |
| `consumer.cpp` | Attaches to the shared objects and runs the consumer threads. |
| `cleanup.cpp` | Removes leftovers after a crash. Safe to run at any time. |
| `Makefile` | `make`, `make run`, `make clean`. |

## Build and run

```
$ cd cpp
$ make
$ ./producer & ./consumer; wait
```
or simply `make run`. The Makefile adds `-lrt` on Linux (needed by glibc older than
2.34) and leaves it out on macOS, where it does not exist. Any C++17 compiler works;
tested with Apple clang 21 and expected to work with g++ 9 or newer.

Expected output (the exact interleaving depends on timing):

```
producer thread created..
consumer thread created..
Producer produces item 1
Producer produces item 2
Consumer consumes item 2
Producer produces item 2
Consumer consumes item 2
Producer produces item 2
Consumer consumes item 2
Producer produces item 2
Consumer consumes item 2
Consumer consumes item 1
```

Both processes exit on their own and nothing is left in `/dev/shm`. If a run is
interrupted (for example with Ctrl-C) and the next `./producer` reports
`shm_open(create /pcp-sharemem): File exists`, run

```
$ ./cleanup
```

If `./consumer` is started without a producer it gives up after ten seconds with
`waiting for /pcp-sharemem: Operation timed out`.

## Configuration

All tunables are `constexpr` values at the top of `protocol.hpp`:

| Name | Default | Meaning (C equivalent) |
|------|---------|------------------------|
| `kBufferSize` | 2 | slots in the buffer (`BUFFER`) |
| `kThreadNum` | 1 | worker threads in each process (`THREAD_NUM`) |
| `kIterations` | 5 | items each thread produces / consumes (`ITR`) |
| `kProducerDelay` | 1 s | sleep before each produce |
| `kConsumerDelay` | 2 s | sleep before each consume |
| `kAttachTimeout` | 10 s | how long the consumer waits for the producer |

The producer and the consumer both run `kThreadNum * kIterations` operations, so the
totals always match and both processes terminate.

## How it works

### The shared segment

```cpp
struct ShmData {
    int x = 0;                       // id of the last item in the "stack"
    std::atomic<bool> ready{false};  // producer sets this once init is complete
    std::atomic<int>  attached{0};   // processes currently mapped
    SharedMutex       mutexBuffer;   // guards x
};
```

`x` plays the same role as in the C version: it is the id of the most recently
produced item and stands in for a real stack. The two new atomics drive the
hand-shake and the clean-up described below. The semaphores are no longer inside the
struct; they are *named* semaphores (`sem_open`) because macOS does not implement
unnamed ones and because a named object is unambiguously shared between processes.

`SharedMemory<ShmData>` sizes the segment with `ftruncate(fd, sizeof(ShmData))` and
maps exactly that many bytes, so the size mismatch of the C version cannot happen.
The producer builds the struct with placement-new:

```cpp
auto shm = SharedMemory<ShmData>::create(kShmName);   // shm_open + ftruncate + mmap
ShmData* data = new (shm.get()) ShmData{};             // construct in shared memory
data->mutexBuffer.init();                              // PTHREAD_PROCESS_SHARED
```

### Start-up hand-shake

The C consumer slept for one second and hoped the producer was ready. The C++
consumer instead:

1. calls `SharedMemory<ShmData>::attach()`, which retries `shm_open()` every 50 ms
   until the segment exists *and* `fstat()` says it has been sized, and
2. calls `waitUntilReady()`, which spins (with the same 50 ms sleep) until the
   producer has stored `true` into `ready` with release semantics.

The producer only sets `ready` after the mutex and both semaphores exist, so the
consumer never sees a half-initialised segment. Both steps are bounded by
`kAttachTimeout`. The consumer can therefore be started before, after or at the same
time as the producer.

### Synchronisation

Exactly as in the textbook solution:

```cpp
// producer thread                       // consumer thread
semEmpty.wait();                         semFull.wait();
{                                        {
    std::lock_guard<SharedMutex> lock(shm.mutexBuffer);
    ++shm.x;                                 std::cout << ... << shm.x;
    std::cout << ... << shm.x;               --shm.x;
}                                        }
semFull.post();                          semEmpty.post();
```

* `semEmpty` starts at `kBufferSize` and counts free slots; the producer blocks on it
  when the buffer is full.
* `semFull` starts at 0 and counts filled slots; the consumer blocks on it when the
  buffer is empty.
* `mutexBuffer` makes the read-modify-write of `x` atomic between all threads of both
  processes. `SharedMutex` wraps a `pthread_mutex_t` created with
  `PTHREAD_PROCESS_SHARED`; it exposes `lock()` / `unlock()`, so the standard
  `std::lock_guard` can be used and the mutex is released even if an exception is
  thrown inside the critical section.

Unlike the C code, every primitive here is explicitly process-shared, so the program
stays correct when the two processes really contend. It was checked with 4 threads
per process, 2000 items per thread and no sleeps: 8000 items produced, 8000
consumed, `x` never left the range 1 to `kBufferSize`, and both processes exited
cleanly.

### Threads

`std::thread` replaces `pthread_create`. Each process keeps a `std::vector<std::thread>`,
starts `kThreadNum` workers with `emplace_back(producer, std::ref(*data), ...)` and
joins them all.

### Who cleans up

`attached` counts the processes currently mapped. The producer sets it to 1 before
publishing `ready`; the consumer increments it after seeing `ready`. On the way out
each process calls `detach()`:

```cpp
if (data.attached.fetch_sub(1) == 1) {   // we were the last one
    data.mutexBuffer.destroy();
    SharedMemory<ShmData>::unlink(kShmName);
    NamedSemaphore::unlink(kSemEmptyName);
    NamedSemaphore::unlink(kSemFullName);
}
```

Whichever process finishes last removes the names. The producer usually finishes
first (5 s against 10 s with the default delays) and in the C version it unlinked
and destroyed objects the consumer was still using. The RAII destructors of
`SharedMemory` and `NamedSemaphore` take care of `munmap()`, `close()` and
`sem_close()` in every process regardless of who unlinks.

### Error handling

All wrappers throw `std::system_error` with the failing call and the `errno` text.
Each `main()` is a function-try-block, so any failure ends in a one-line message on
`stderr` and `EXIT_FAILURE`, for example:

```
producer: shm_open(create /pcp-sharemem): File exists
producer: if objects were left over from an earlier run, run ./cleanup
```

`sem_wait()` is restarted on `EINTR`.

## Differences from the C version at a glance

| | C (2021) | C++ (2026) |
|---|---|---|
| Semaphores | unnamed, `sem_init(pshared = 0)`, inside the struct | named, `sem_open()`, process-shared by construction |
| Mutex | default attributes | `PTHREAD_PROCESS_SHARED`, wrapped for `std::lock_guard` |
| Segment size | 32 bytes for an 80 to 112 byte struct | exactly `sizeof(ShmData)` |
| Consumer start | fixed `sleep(1)` | retry loop + `ready` flag, 10 s timeout |
| Loop counts | consumer does `ITR + 3`, hangs after `ITR` | both do `kIterations`, both exit |
| Teardown | both processes destroy and unlink | last process out destroys and unlinks |
| Threads | `pthread_create` with a mis-typed function | `std::thread` |
| Errors | mostly unchecked | exceptions carrying `errno`, one catch per program |
| Resource release | manual | RAII destructors |
| Platforms | Linux only | Linux and macOS |

## Portability notes

* macOS limits semaphore names to 31 characters (`PSEMNAMLEN`); the names used here
  are well under that. `sem_getvalue()` is not available on macOS and is not used.
* `std::atomic<int>` and `std::atomic<bool>` are checked with `static_assert` to be
  lock-free, which is what makes them usable from two different processes.
* The programs were built and run on macOS 26 (Apple clang 21, arm64). On Linux build
  with the same `make`; nothing in the code is platform-specific.

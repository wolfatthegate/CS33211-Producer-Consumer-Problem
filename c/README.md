# Producer-Consumer Problem (C version, 2021)

Author - Waylon Luo (Kent State University)

This is the original assignment submission, written in C in April 2021. It is kept
unchanged for reference; a C++ rewrite that fixes the issues listed at the bottom of
this page lives in [`../cpp/`](../cpp/README.md).

Producer-Consumer Problem is a classical synchronization problem where the producer
produces items and place them in the buffer - the consumer consumes the items from the
buffer. In this assignment, the producer and the consumer run under two seperate
processes while sharing the stack pointer, semaphore and mutex.

This project contains four files.
1) producer.c
2) consumer.c
3) protocol.h
4) cleanup.c

To compile and run the program use the following commands (from inside this `c/`
directory).
```
$ cd c
$ gcc producer.c -pthread -lrt -o producer
$ gcc consumer.c -pthread -lrt -o consumer
$ gcc cleanup.c -pthread -lrt -o cleanup
$ ./producer & ./consumer &
```

The output should look like:

```
producer thread creaded..
consumer thread creaded..
Producer produces item 1
Producer produces item 2
Consumer consumes item 2
Producer produces item 2
Consumer consumes item 2
Consumer consumes item 1
```
If the program crashes in the middle of the process and does not have a chance to
clean up the shared memory space, run
```
$ ./cleanup
```

Note: The program does not run on macOS. It only runs on Linux server or Virtual box.
(See "Why it does not run on macOS" below.)

## Descriptions

`protocol.h` has the global variables for both `producer.c` and `consumer.c`. The
number of threads, the buffer size and iterations can be adjusted in `protocol.h`. It
has a data structure, `struct ShmData` which both `producer.c` and `consumer.c` share.

```
struct ShmData {
	int x;
	sem_t semEmpty;
	sem_t semFull;
	pthread_mutex_t mutexBuffer;
};
```
The usage of shared memory data, `ShmData`, is discussed in `producer()` and
`consumer()`.

#### `main()` method

`shm_open()` is used to create shared memory as followed.
```
int fd = shm_open(NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
```

`consumer.c` also has to call `shm_open()` to access the share memory without passing
`O_CREAT`, meaning that it is accessing the shared memory that `producer.c` created; it
does not have to reserve share memory.

`struct ShmData *shmData` is mapped in the shared memory.
```
struct ShmData *shmData = mmap(0, sizeof(*shmData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
```
As default, the number of threads is set to 1,which can be modified in `protocol.h`,
`THREAD_NUM`. The buffer size is set to 2, meaning that there are two slots for items.
At the end of the program, semaphores and mutex are destory, shared memory data are
unmapped and shared memory is unlinked.

#### `void* producer(struct ShmData *shmData);` and `void* consumer(struct ShmData *shmData);`

The iterations are limited to a certain number for a demonstration purpose. One can
modify the iteration `ITR` in `protocol.h`.

`shmData->x` is used as a stack pointer or the ID of the last item in the stack. For
simplicity of the problem, we do not have an actual stack in our programs.

Two semaphores, `shmData->semEmpty` and `shmData->semFull` are used so that the producer
will not be skipping while the buffer is full, or the consumer will not be skipping
while the buffer is empty.

`shmData->mutexBuffer` is used to make sure that only one thread is accessing
`shmData->x` at a time.

## Review notes (October 2026)

The code was re-read five years later before being rewritten in C++. The
synchronisation idea is right (two counting semaphores plus a mutex around the shared
counter), but the implementation has a number of problems. They are listed here so the
C++ version can be compared against them; the C sources themselves were left as
submitted.

### Correctness

1. **The semaphores and the mutex are not process-shared.**
   `sem_init(&sem, 0, n)` has `pshared = 0`, and the mutex is created with default
   attributes. POSIX only guarantees such objects between threads of *one* process.
   On glibc they are implemented with private futexes, so when one process really
   blocks inside `sem_wait()` or `pthread_mutex_lock()`, the wake-up issued by the
   other process may never reach it. The program usually works anyway because the
   `sleep(1)` / `sleep(2)` calls keep the two processes from ever contending.
   The fix is `pshared = 1` for the semaphores and
   `pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)` for the mutex.

2. **The shared segment is too small.**
   `SIZE` is `NUM * sizeof(int)` = 32 bytes, but `struct ShmData` is 80 bytes on
   macOS/arm64 and about 112 bytes on x86-64 Linux. `ftruncate(fd, SIZE)` therefore
   undersizes the segment and `munmap(shmData, SIZE)` unmaps too little. It does not
   crash only because `mmap()` works in whole pages, so the rest of the struct lands
   in the same page. It should be `ftruncate(fd, sizeof(struct ShmData))`.

3. **The consumer never exits.**
   `consumer()` loops `ITR + 3` times while `producer()` only produces `ITR` items.
   After consuming the fifth item the consumer blocks forever on the sixth
   `sem_wait(&semFull)`. The process has to be killed and `./cleanup` run afterwards.

4. **Both processes tear everything down.**
   Producer and consumer each call `sem_destroy()`, `pthread_mutex_destroy()` and
   `shm_unlink()` at exit. The producer finishes first (5 s versus 10 s) and destroys
   objects the consumer is still using. Only the last process to leave should do this.

5. **Start-up race.**
   The consumer relies on a fixed `sleep(1)` for the producer to have created and
   initialised the segment. If the producer is slow, or the consumer is started
   first, `shm_open()` fails (or the consumer maps an uninitialised struct).

### Code quality

6. **Wrong thread-function type.**
   `void* producer(struct ShmData*)` is passed where `pthread_create()` expects
   `void* (*)(void*)`. GCC 13 and older emit a warning; GCC 14 and Apple clang treat
   it as an error (see the macOS note below). The functions also have no `return`
   statement although they are declared to return `void*`.

7. **Unchecked system calls.** `ftruncate()` and `mmap()` results are not tested;
   `mmap()` returning `MAP_FAILED` would be dereferenced.

8. **Small things.** `perror("...\n")` prints the newline *before* the
   `: <error text>` suffix; `close(fd)` is called with a negative descriptor on the
   error path; `cleanup.c` unconditionally prints a `perror()` message even when
   nothing was wrong; the `NUM` and `SIZE` macros are only needed because of issue 2.

### Why it does not run on macOS

* Apple clang rejects the thread-function pointer conversion in issue 6 as a hard
  error, so the files do not compile at all.
* macOS does not implement unnamed semaphores: `sem_init()` is deprecated and
  returns `ENOSYS`. Only named semaphores (`sem_open()`) are available.
* There is no `librt` on macOS, so `-lrt` makes the link step fail.

All of the above are addressed in the [C++ version](../cpp/README.md).

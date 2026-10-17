# CS33211-Assignment

Author - Waylon Luo (Kent State University)

Producer-Consumer Problem is a classical synchronization problem where the producer
produces items and places them in the buffer and the consumer consumes the items from
the buffer. In this assignment, the producer and the consumer run in two separate
processes while sharing the stack pointer, the semaphores and the mutex through POSIX
shared memory.

The repository holds two implementations of the same program. Each has its own
documentation.

| Version | Directory | Documentation | Runs on |
|---------|-----------|---------------|---------|
| Original C submission (April 2021) | [`c/`](c/) | [c/README.md](c/README.md) | Linux |
| C++17 rewrite (October 2026) | [`cpp/`](cpp/) | [cpp/README.md](cpp/README.md) | Linux, macOS |

The C version is kept exactly as it was submitted. Its documentation ends with a list
of the problems found when it was re-read in 2026. The C++ version fixes those
problems, uses RAII wrappers, `std::thread` and `std::lock_guard`, and keeps the
original behaviour and output.

## Quick start

C++ version:
```
$ cd cpp
$ make run
```

C version (Linux only):
```
$ cd c
$ gcc producer.c -pthread -lrt -o producer
$ gcc consumer.c -pthread -lrt -o consumer
$ ./producer & ./consumer &
```

#pragma once


#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

//attach a posix shared memory segment of the given name and size, returning a pointer to it

static inline void* shm_attach(const char* name, size_t size, bool create){
    int flags = 0_RDWR | (create ? 0_CREAT : 0);
    int fd = shm_open(name, flags, 0600);
    if(fd<0){perrpr("shm_open");std::exist(1);}
    if(create){
        if(ftruncate(fd,size)<0){perror("ftruncate"); std::exist(1);}
    }
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE,MAP_SHARED, fd,0);
    if(p==MAP_FAILED){perror("mmap"); std::exist(1);}

    close(fd);
    return p;
}



// Section breakdown
// shm_open(name, flags, mode)
// Opens a POSIX shared-memory object — a kernel-managed file that lives in /dev/shm/. Returns a file descriptor.

// name — must start with /. The actual path is /dev/shm/<name> minus the leading slash.
// O_RDWR | O_CREAT — read/write, create if it doesn't exist.
// 0600 — file permissions (owner read/write only).
// ftruncate(fd, size)
// Sets the shared-memory region to size bytes. Without this, the region has zero size and mmap will fail.

// This is idempotent — if the file already exists at the right size, this is a no-op. So both producer and consumer can safely call it; whoever runs first does the real work.

// mmap(...)
// Maps the shared-memory file into the process's address space.

// nullptr — let the kernel pick where to map it.
// PROT_READ | PROT_WRITE — both processes can read and write.
// MAP_SHARED — critical. Without this flag, writes wouldn't be visible to the other process.
// Returns a pointer; both processes will get different virtual addresses but they point to the same physical pages.
// close(fd) — why is this safe?
// mmap holds its own internal reference to the file. Once the memory is mapped, the file descriptor isn't needed. Closing it frees a process resource without affecting the mapping.

// Error handling
// perror prints a human-readable error to stderr, then exit(1). Crude but fine for a benchmark.


// SpeedBreaker PS5: protection changes must enter through libkernel.
#include <cerrno>
#include <cstdint>
#include <sys/mman.h>
#include <ps5platform/kernel.h>

extern "C" int sceKernelMmap(void*, size_t, int, int, int, off_t, void**);

// The payload SDK's mprotect issues syscall 74 directly for non-executable
// pages. Native titles may only issue it from libkernel: otherwise the kernel
// terminates the title with SYSTEM_ILLEGAL_FUNCTION_CALL (PPRBUG-22859).
// Wrap every caller, including the guest null guard and GPU write watch.
extern "C" int __wrap_mprotect(const void* address, size_t length, int protection)
{
    const int32_t rc = sceKernelMprotect(address, length, protection);
    if (rc == 0)
        return 0;

    // SCE kernel errors encode the FreeBSD errno in the low 16 bits.
    errno = static_cast<int>(static_cast<uint32_t>(rc) & 0xffffu);
    return -1;
}

// The SDK's mmap is in the same mman.o and also issues direct syscalls.
// XEX file mappings and Mesa's cache need the libkernel entry point too.
extern "C" void* __wrap_mmap(void* address, size_t length, int protection,
                            int flags, int fd, off_t offset)
{
    void* mapped = nullptr;
    const int rc = sceKernelMmap(address, length, protection, flags, fd, offset, &mapped);
    if (rc == 0)
        return mapped;

    errno = static_cast<int>(static_cast<uint32_t>(rc) & 0xffffu);
    return MAP_FAILED;
}

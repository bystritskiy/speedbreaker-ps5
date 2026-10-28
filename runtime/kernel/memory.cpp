// Adapted from Unleashed Recompiled (https://github.com/hedge-dev/UnleashedRecomp),
// GPL-3.0-or-later. Modified for SpeedBreaker.
#include "../stdafx.h"
#include "memory.h"
#ifdef _WIN32
#include <platform/memory.h>
#endif

#ifdef __PROSPERO__
#include <cerrno>
#include <sys/mman.h>
#include <ps5platform/shm.h>

// SpeedBreaker PS5: the guest's 4 GB is a reserved range (ps5_vrange_reserve),
// filled with views of direct memory (ps5platform/shm.h): one object for
// everything below the physical windows (virtual heaps, the image, the
// function table), and one 512 MB object for physical memory, mapped at all
// three windows. Anonymous memory would be charged to the title's small
// flexible budget; direct memory is charged to the direct pool only, and one
// object can be mapped any number of times. Everything is committed up front
// (3 GB), as UnleashedRecomp-Prospero does; vmem.cpp then clears released
// pages in place instead of remapping them.
bool Memory::MapPhysicalMirrors()
{
    constexpr size_t PHYSICAL_SIZE = 0x20000000;  // 512 MB
    struct ps5_shm phys{};
    int rc = ps5_shm_create(PHYSICAL_SIZE, &phys);
    if (rc != 0)
    {
        fprintf(stderr, "[memory] ps5_shm_create(physical): 0x%x\n", unsigned(rc));
        failedError = rc;
        return false;
    }
    // The 0xE0000000 window's 4 KB shift is not a 64 KB direct-memory unit:
    // unshifted, as on 16 KB-page Apple hosts (MmGetPhysicalAddress uses
    // eWindowShift).
    eWindowShift = 0;
    for (uint32_t guest : { 0xA0000000u, 0xC0000000u, 0xE0000000u })
    {
        void* view = nullptr;
        rc = ps5_shm_map(&phys, 0, PHYSICAL_SIZE, base + guest, PS5_SHM_READ | PS5_SHM_WRITE, PS5_SHM_FIXED, &view);
        if (rc != 0 || view != base + guest)
        {
            fprintf(stderr, "[memory] ps5_shm_map(physical at 0x%08X): 0x%x\n", guest, unsigned(rc));
            failedError = rc;
            return false;
        }
    }
    return true;
}

Memory::Memory()
{
    constexpr size_t LOW_SIZE = 0xA0000000;  // below the physical windows
    void* range = nullptr;
    int rc = ps5_vrange_reserve(PPC_MEMORY_SIZE, (void*)0x1000000000ull, 0x10000, &range);
    if (rc != 0 || range == nullptr)
    {
        fprintf(stderr, "[memory] ps5_vrange_reserve(4 GB): 0x%x\n", unsigned(rc));
        failedStep = 1;
        failedError = rc ? rc : ENOMEM;
        return;
    }
    base = static_cast<uint8_t*>(range);
    struct ps5_shm low{};
    void* view = nullptr;
    rc = ps5_shm_create(LOW_SIZE, &low);
    if (rc == 0)
        rc = ps5_shm_map(&low, 0, LOW_SIZE, base, PS5_SHM_READ | PS5_SHM_WRITE, PS5_SHM_FIXED, &view);
    if (rc != 0 || view != base)
    {
        fprintf(stderr, "[memory] direct memory for the guest's low 2.5 GB: 0x%x\n", unsigned(rc));
        failedStep = 1;
        failedError = rc ? rc : ENOMEM;
        base = nullptr;
        return;
    }
    // Guest null pointers fault: the first host page (16 KB) is not accessible.
    if (mprotect(base, 0x4000, PROT_NONE) != 0)
    {
        failedStep = 1;
        failedError = errno;
        fprintf(stderr, "[memory] guest null-page protection: errno %d\n", failedError);
        base = nullptr;
        return;
    }
    if (!MapPhysicalMirrors())
    {
        failedStep = 2;
        base = nullptr;
        return;
    }
    fprintf(stderr, "[memory] guest 4 GB at %p in direct memory\n", static_cast<void*>(base));

    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        if (PPCFuncMappings[i].host != nullptr)
            InsertFunction(PPCFuncMappings[i].guest, PPCFuncMappings[i].host);
    }
}
#elif !defined(_WIN32)
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

bool Memory::MapPhysicalMirrors()
{
    constexpr size_t PHYSICAL_SIZE = 0x20000000;  // 512 MB
#ifdef __APPLE__
    // A named memory entry, mapped three times with vm_map. The iOS sandbox
    // refuses shm_open (EPERM); this works there and on macOS alike.
    memory_object_size_t entrySize = PHYSICAL_SIZE;
    mach_port_t entry = MACH_PORT_NULL;
    kern_return_t kr = mach_make_memory_entry_64(mach_task_self(), &entrySize, 0,
        MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &entry, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "[memory] mach_make_memory_entry_64: %s\n", mach_error_string(kr));
        failedError = kr;
        return false;
    }
    bool ok = true;
#else
    char name[64];
    snprintf(name, sizeof(name), "/speedbreaker-%d", int(getpid()));
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
    {
        failedError = errno;
        perror("[memory] shm_open");
        return false;
    }
    shm_unlink(name);  // the mappings keep it alive
    bool ok = ftruncate(fd, PHYSICAL_SIZE) == 0;
    if (!ok)
        failedError = errno;
#endif

    struct View { uint32_t guest; size_t offset; size_t length; };
    const View views[] = {
        { 0xA0000000, 0, PHYSICAL_SIZE },
        { 0xC0000000, 0, PHYSICAL_SIZE },
        { 0xE0000000, 0x1000, PHYSICAL_SIZE - 0x1000 },
    };
    for (const View& v : views)
    {
        if (!ok)
            break;
        void* at = base + v.guest;
        // mmap offsets must be host-page aligned. On 16 KB-page hosts (Apple
        // Silicon) the 0xE0000000 window's 4 KB shift can't be expressed, so
        // it is mapped unshifted there; MmGetPhysicalAddress uses
        // eWindowShift so addresses stay consistent. 4 KB-page hosts (x86
        // Linux, the Steam Deck) get the exact hardware layout.
        size_t offset = v.offset;
        if (offset % size_t(sysconf(_SC_PAGESIZE)) != 0)
            offset = 0;
        if (v.guest == 0xE0000000)
            eWindowShift = uint32_t(offset);
#ifdef __APPLE__
        vm_address_t address = vm_address_t(at);
        kr = vm_map(mach_task_self(), &address, v.length, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, entry,
            vm_offset_t(offset), FALSE, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
        if (kr != KERN_SUCCESS)
        {
            fprintf(stderr, "[memory] vm_map physical view: %s\n", mach_error_string(kr));
            failedError = kr;
            ok = false;
        }
#else
        if (mmap(at, v.length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, off_t(offset)) == MAP_FAILED)
        {
            failedError = errno;
            perror("[memory] mmap physical view");
            ok = false;
        }
#endif
    }
#ifdef __APPLE__
    mach_port_deallocate(mach_task_self(), entry);  // the mappings keep it alive
#else
    close(fd);
#endif
    return ok;
}
#endif

#ifndef __PROSPERO__
Memory::Memory()
{
#ifdef _WIN32
    // SpeedBreaker: the same layout through placeholders: the physical
    // windows alias one section, the rest is private (platform/memory.h).
    // A view must fill its placeholder, so the 0xE window is unshifted.
    base = platform::MapGuestSpace();
    if (base == nullptr)
        return;
    eWindowShift = 0;
#else
    // NFSMW_TEST_RESERVE_FAIL=1|2: fail the reservation or the mirrors as an
    // iOS without room would (memory_status.h), to see the startup message
    // anywhere.
    const char* testFail = std::getenv("NFSMW_TEST_RESERVE_FAIL");
    int failStep = testFail ? std::atoi(testFail) : 0;

    // SpeedBreaker: one contiguous 4 GB, since guest addresses are offsets
    // from base. iOS makes room for it with Extended Virtual Addressing or
    // Increased Memory Limit (scripts/ios/SpeedBreaker/SpeedBreaker.entitlements,
    // kernel/memory_status.h); without them only about 5 GB is free in one
    // piece, so this runs in static initialization, before anything else
    // takes address space. If it fails (ENOMEM), main.cpp says so on screen.
    base = failStep == 1 ? (uint8_t*)MAP_FAILED
        : (uint8_t*)mmap((void*)0x100000000ull, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == (uint8_t*)MAP_FAILED && failStep != 1)
        base = (uint8_t*)mmap(NULL, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == (uint8_t*)MAP_FAILED)
    {
        failedStep = 1;
        failedError = failStep == 1 ? ENOMEM : errno;
        base = nullptr;
        return;
    }

    mprotect(base, 4096, PROT_NONE);

    // SpeedBreaker: the 360 exposes its 512 MB of physical memory through three
    // windows that alias each other: 0xA0000000 and 0xC0000000 map physical 0,
    // 0xE0000000 maps physical +0x1000 (Xenia's map_info). D3D writes records
    // through one window and reads them through another, so all three must be
    // the same host memory: one shared object, mapped three times.
    if (failStep == 2 || !MapPhysicalMirrors())
    {
        failedStep = 2;
        if (failStep == 2)
        {
#ifdef __APPLE__
            failedError = KERN_NO_SPACE;
#else
            failedError = ENOMEM;
#endif
        }
        munmap(base, PPC_MEMORY_SIZE);
        base = nullptr;
        return;
    }
#endif

    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        if (PPCFuncMappings[i].host != nullptr)
            InsertFunction(PPCFuncMappings[i].guest, PPCFuncMappings[i].host);
    }
}
#endif

void* MmGetHostAddress(uint32_t ptr)
{
    return g_memory.Translate(ptr);
}

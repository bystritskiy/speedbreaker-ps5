// libc++'s filesystem uses POSIX extensions missing from the PS5 libc.
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

// The system realpath fails even with caller-owned storage in the native
// title. libc++ 18 weakly_canonical then drops that error and returns just
// the nonexistent suffix ("game"). lstat is refused with EPERM even on
// /data. Resolve existing paths with open(O_NOFOLLOW) and fstat instead.
// The pinned SDK's readlink is a forbidden raw syscall: reject symlinks
// rather than silently resolving them wrongly.
// PS5's /app0, /data and /mnt/usb* are mounts, not symlinks.
extern "C" char* __wrap_realpath(const char* path, char* output)
{
    if (!path)
    {
        errno = EINVAL;
        return nullptr;
    }
    if (!*path)
    {
        errno = ENOENT;
        return nullptr;
    }
    char absolute[PATH_MAX];
    size_t length = 0;
    if (path[0] != '/')
    {
        if (!getcwd(absolute, sizeof(absolute)))
        {
            const int error = errno;
            std::fprintf(stderr, "[ps5 path] getcwd failed: %d (%s)\n", error, std::strerror(error));
            errno = error;
            return nullptr;
        }
        length = std::strlen(absolute);
        if (!length || absolute[0] != '/')
        {
            errno = EIO;
            return nullptr;
        }
        if (length > 1 && absolute[length - 1] == '/')
            --length;
        if (length > 1)
            absolute[length++] = '/';
    }
    const size_t inputLength = std::strlen(path);
    if (inputLength >= sizeof(absolute) - length)
    {
        errno = ENAMETOOLONG;
        return nullptr;
    }
    std::memcpy(absolute + length, path, inputLength + 1);

    char resolved[PATH_MAX] = "/";
    length = 1;
    const char* at = absolute;
    while (*at)
    {
        while (*at == '/') ++at;
        if (!*at) break;
        const char* begin = at;
        while (*at && *at != '/') ++at;
        const size_t count = size_t(at - begin);
        if (count == 1 && begin[0] == '.')
            continue;
        if (count == 2 && begin[0] == '.' && begin[1] == '.')
        {
            while (length > 1 && resolved[length - 1] != '/') --length;
            if (length > 1) --length;
            resolved[length] = '\0';
            continue;
        }
        if (count > NAME_MAX)
        {
            errno = ENAMETOOLONG;
            return nullptr;
        }
        if (length > 1) resolved[length++] = '/';
        // absolute already fits PATH_MAX; removing dot components cannot
        // increase the result's length.
        std::memcpy(resolved + length, begin, count);
        length += count;
        resolved[length] = '\0';
        int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW;
        if (*at) flags |= O_DIRECTORY;
        const int fd = open(resolved, flags);
        if (fd < 0)
        {
            const int error = errno;
            std::fprintf(stderr, "[ps5 path] open %s failed: %d (%s)\n", resolved, error, std::strerror(error));
            errno = error;
            return nullptr;
        }
        struct stat status;
        const int info = fstat(fd, &status);
        const int error = errno;
        close(fd);
        if (info != 0)
        {
            std::fprintf(stderr, "[ps5 path] fstat %s failed: %d (%s)\n", resolved, error, std::strerror(error));
            errno = error;
            return nullptr;
        }
        if (*at && !S_ISDIR(status.st_mode))
        {
            errno = ENOTDIR;
            return nullptr;
        }
    }
    if (output)
    {
        std::memcpy(output, resolved, length + 1);
        return output;
    }
    char* result = static_cast<char*>(std::malloc(length + 1));
    if (!result)
    {
        errno = ENOMEM;
        return nullptr;
    }
    std::memcpy(result, resolved, length + 1);
    return result;
}

// lstat is refused with EPERM even on /data, so libc++'s symlink_status
// reported every path as missing: the installer never saw (or cleared) the
// game.partial an interrupted install left. Answer from open(O_NOFOLLOW) and
// fstat; ELOOP from it means a symlink, which is all callers need to know.
extern "C" int __real_lstat(const char* path, struct stat* status);
extern "C" int __wrap_lstat(const char* path, struct stat* status)
{
    if (const int result = __real_lstat(path, status); result == 0 || errno != EPERM)
        return result;
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
    {
        if (errno != ELOOP)
            return -1;
        std::memset(status, 0, sizeof(*status));
        status->st_mode = S_IFLNK | 0777;
        return 0;
    }
    const int result = fstat(fd, status);
    const int error = errno;
    close(fd);
    errno = error;
    return result;
}

// The SDK's pathconf is a raw syscall (191), forbidden in native titles.
// libc++ asks for _PC_PATH_MAX before getcwd. These are the SDK's declared
// filesystem limits; preserve missing/inaccessible-path errors via stat.
extern "C" long __wrap_pathconf(const char* path, int name)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    struct stat status;
    if (stat(path, &status) != 0)
        return -1;
    switch (name)
    {
    case _PC_PATH_MAX: return PATH_MAX;
    case _PC_NAME_MAX: return NAME_MAX;
    default:
        errno = EINVAL;
        return -1;
    }
}

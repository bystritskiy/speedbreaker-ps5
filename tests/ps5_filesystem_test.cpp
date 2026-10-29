// Host regression for PS5's native-title path resolution and launch args.
// clang++ -std=c++20 -O2 -Iport/ps5/src tests/ps5_filesystem_test.cpp \
//   port/ps5/src/filesystem_ps5.cpp -o build/ps5_filesystem_test
#include "startup_args.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" char* __wrap_realpath(const char*, char*);
extern "C" long __wrap_pathconf(const char*, int);
extern "C" int __wrap_lstat(const char*, struct stat*);

// The native title's lstat: refused even on /data.
static bool lstatRefused = true;
extern "C" int __real_lstat(const char* path, struct stat* status)
{
    if (!lstatRefused)
        return lstat(path, status);
    errno = EPERM;
    return -1;
}

int main()
{
    char executable[] = "/app0/eboot.bin", title[] = "PPSA90505", option[] = "--verify", image[] = "disc.iso";
    char* launcher[] = {executable, title, nullptr};
    int argc = 2;
    ps5::startup::RemoveTitleArgument(argc, launcher);
    assert(argc == 1 && launcher[0] == executable && !launcher[1]);
    char* cli[] = {executable, title, option, image, nullptr};
    argc = 4; ps5::startup::RemoveTitleArgument(argc, cli);
    assert(argc == 3 && cli[1] == option && cli[2] == image && !cli[3]);
    char* direct[] = {executable, image, nullptr};
    argc = 2; ps5::startup::RemoveTitleArgument(argc, direct);
    assert(argc == 2 && direct[1] == image);
    ps5::startup::RemoveTitleArgument(argc = 0, nullptr);

    namespace fs = std::filesystem;
    char scratch[] = "/tmp/speedbreaker-paths-XXXXXX";
    const char* made = mkdtemp(scratch); assert(made);
    fs::path root = fs::canonical(made);
    fs::create_directory(root / "data");
    fs::create_directory_symlink(root / "data", root / "link");
    const std::string normalized = (root / "data" / "..").string();
    char* allocated = __wrap_realpath(normalized.c_str(), nullptr);
    assert(allocated && fs::path(allocated) == root); free(allocated);
    char output[PATH_MAX];
    assert(__wrap_realpath((root / "data").c_str(), output) == output && fs::path(output) == root / "data");
    assert(!__wrap_realpath((root / "missing").c_str(), nullptr) && errno == ENOENT);
    assert(!__wrap_realpath(nullptr, nullptr) && errno == EINVAL);
    assert(!__wrap_realpath("", nullptr) && errno == ENOENT);
    assert(!__wrap_realpath((root / "link").c_str(), nullptr) && errno == ELOOP);
    fs::create_directory_symlink(root / "absent", root / "dangling");
    assert(!__wrap_realpath((root / "dangling").c_str(), nullptr) && errno == ELOOP);
    assert(!__wrap_realpath((root / "link" / "child").c_str(), nullptr) && (errno == ELOOP || errno == ENOTDIR));
    FILE* file = fopen((root / "file").c_str(), "wb"); assert(file); fclose(file);
    allocated = __wrap_realpath((root / "file").c_str(), nullptr);
    assert(allocated && fs::path(allocated) == root / "file"); free(allocated);
    assert(!__wrap_realpath((root / "file" / "..").c_str(), nullptr) && errno == ENOTDIR);
    assert(!__wrap_realpath((root / "file").string().append("/").c_str(), nullptr) && errno == ENOTDIR);
    assert(!__wrap_realpath((root / std::string(NAME_MAX + 1, 'a')).c_str(), nullptr) && errno == ENAMETOOLONG);
    assert(!__wrap_realpath(std::string(PATH_MAX, '/').c_str(), nullptr) && errno == ENAMETOOLONG);
    char old[PATH_MAX]; assert(getcwd(old, sizeof(old)));
    assert(chdir((root / "data").c_str()) == 0);
    allocated = __wrap_realpath(".././data//", nullptr);
    assert(allocated && fs::path(allocated) == root / "data"); free(allocated);
    allocated = __wrap_realpath(".", nullptr);
    assert(allocated && fs::path(allocated) == root / "data"); free(allocated);
    assert(chdir(old) == 0);
    allocated = __wrap_realpath("/../../", nullptr);
    assert(allocated && std::strcmp(allocated, "/") == 0); free(allocated);
    assert(__wrap_pathconf(root.c_str(), _PC_PATH_MAX) == PATH_MAX);
    assert(__wrap_pathconf(root.c_str(), _PC_NAME_MAX) == NAME_MAX);
    assert(__wrap_pathconf((root / "missing").c_str(), _PC_PATH_MAX) == -1 && errno == ENOENT);
    assert(__wrap_pathconf(root.c_str(), -1234) == -1 && errno == EINVAL);
    struct stat status;
    assert(__wrap_lstat((root / "data").c_str(), &status) == 0 && S_ISDIR(status.st_mode));
    assert(__wrap_lstat((root / "file").c_str(), &status) == 0 && S_ISREG(status.st_mode));
    assert(__wrap_lstat((root / "link").c_str(), &status) == 0 && S_ISLNK(status.st_mode));
    assert(__wrap_lstat((root / "dangling").c_str(), &status) == 0 && S_ISLNK(status.st_mode));
    assert(__wrap_lstat((root / "missing").c_str(), &status) == -1 && errno == ENOENT);
    lstatRefused = false;
    assert(__wrap_lstat((root / "missing").c_str(), &status) == -1 && errno == ENOENT);
    assert(__wrap_lstat((root / "link").c_str(), &status) == 0 && S_ISLNK(status.st_mode));
    fs::remove_all(root);
    puts("PASS: PS5 launch arguments, absolute/relative paths, dot components, symlink refusal, lstat fallback, limits and errors");
}

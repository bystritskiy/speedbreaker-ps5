// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The game's files read from the player's disc image where it is, rather than
// from a copy (install::UseImage). The PS5 meters what an application writes
// to its storage: a copy of the disc falls to about 2 MB/s after its first
// 2.5 GB (port/ps5/README.md).
//
// Once mounted, the image's path is the game folder (SetGamePath): a host
// path below it, <image>/NFS/ZZDATA0.BIN, names that file in the image. The
// file system exports (kernel/vfs.cpp, kernel/imports_file.cpp) ask here
// before the host file system. XDVDFS stores each file in one run of
// sectors, so a file is its offset and size in the image.
#pragma once
#include "result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace install::image
{
    // Reads the image's file tree. Once, before the game starts; the index
    // is read-only after that, from any thread.
    Result Mount(const std::filesystem::path& image);

    // `host` is the mounted image or a path below it.
    bool Contains(const std::filesystem::path& host);

    struct Entry
    {
        bool directory = false;
        uint64_t offset = 0;     // in the image
        uint64_t size = 0;       // 0 for a directory
    };

    // The file or directory at `host`, matched ASCII case-insensitively.
    std::optional<Entry> Find(const std::filesystem::path& host);

    // `dir`'s child named `name` (any case), as the disc spells it.
    std::optional<std::filesystem::path> Child(const std::filesystem::path& dir, std::string_view name);

    // The paths of the entries in the directory `dir`, disc spelling.
    std::vector<std::filesystem::path> List(const std::filesystem::path& dir);

    // A new read-only descriptor of the image, for the caller to close.
    int OpenImage();

    // The image's modification time, as the game is shown its files' times.
    void ModifiedTime(int64_t& seconds, int64_t& nanoseconds);

    // A whole file of the image (default.xex, NFS/ZDIR.BIN).
    bool ReadAll(const std::filesystem::path& host, std::vector<uint8_t>& data);
}
